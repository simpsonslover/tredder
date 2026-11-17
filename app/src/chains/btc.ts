// Bitcoin: balances/history via Esplora (mempool.space), PSBT construction for
// the device, finalisation and broadcast. No keys: the device signs the PSBT.
import * as ecc from '@bitcoinerlab/secp256k1';
import { bytesToHex, hexToBytes } from '@noble/hashes/utils.js';
import { Psbt, address as baddress, type Network } from 'bitcoinjs-lib';

import type { Net, Scheme } from '../protocol/types';
import { HttpError, getJson, getText, pool, postText } from '../util/http';
import { btcNetwork, derivePubkey, fullPath, scriptFor, type XpubKey } from './btcDerive';
import { CHAINS } from './config';
import type { TxRecord } from './types';

const servers = (net: Net) => {
  const c = CHAINS.btc.net[net];
  return [c.esplora!, ...(c.esploraFallbacks ?? [])];
};
const TIMEOUT_MS = 10_000;
const preferred: Partial<Record<Net, number>> = {};

/** A real answer (e.g. 404 tx not found, 400 tx rejected) is final. An unreachable, overloaded (429,
 *  5xx) or filtered (403, bot page) server moves on to the next one. */
const isFinal = (e: unknown) => e instanceof HttpError && e.status >= 400 && e.status < 500 && e.status !== 403 && e.status !== 429;

/* At most MAX_PER_SERVER requests in flight per server: an account scan is ~80 lookups, and a burst
   of them gets HTTP 429 from Blockstream (seen 2026-11-16). */
const MAX_PER_SERVER = 4;
const inFlight = new Map<string, number>();
const queued = new Map<string, (() => void)[]>();
async function withSlot<T>(base: string, fn: () => Promise<T>): Promise<T> {
  while ((inFlight.get(base) ?? 0) >= MAX_PER_SERVER) await new Promise<void>((res) => queued.set(base, [...(queued.get(base) ?? []), res]));
  inFlight.set(base, (inFlight.get(base) ?? 0) + 1);
  try {
    return await fn();
  } finally {
    inFlight.set(base, inFlight.get(base)! - 1);
    const q = queued.get(base);
    q?.shift()?.();
  }
}
const RETRY_429_MS = [1000, 2000];
export const _test = { RETRY_429_MS };

/** Run one Esplora call on the server that worked last, then the others in order. A rate limit
 *  (429) is waited out twice on the same server before moving on. */
export async function esplora<T>(net: Net, run: (base: string) => Promise<T>): Promise<T> {
  const list = servers(net);
  const start = preferred[net] ?? 0;
  let err: unknown;
  for (let k = 0; k < list.length; k++) {
    const i = (start + k) % list.length;
    const base = list[i]!;
    for (let attempt = 0; ; attempt++) {
      try {
        const r = await withSlot(base, () => run(base));
        preferred[net] = i;
        return r;
      } catch (e) {
        if (isFinal(e)) throw e;
        err = e;
        const wait = e instanceof HttpError && e.status === 429 ? _test.RETRY_429_MS[attempt] : undefined;
        if (wait === undefined) break;
        await new Promise((res) => setTimeout(res, wait));
      }
    }
  }
  throw err;
}
const ejson = <T>(net: Net, path: string) => esplora(net, (b) => getJson<T>(`${b}${path}`, TIMEOUT_MS));
export const GAP_LIMIT = 20;
export const DUST = 546n;

interface AddrStats {
  chain_stats: { funded_txo_sum: number; spent_txo_sum: number; tx_count: number };
  mempool_stats: { funded_txo_sum: number; spent_txo_sum: number; tx_count: number };
}
interface EsploraUtxo {
  txid: string;
  vout: number;
  value: number;
  status: { confirmed: boolean };
}

export interface Utxo {
  txid: string;
  vout: number;
  value: bigint;
  confirmed: boolean;
  change: number;
  index: number;
  address: string;
}

export interface BtcAccountState {
  balance: bigint; // confirmed + unconfirmed
  utxos: Utxo[];
  nextReceive: number;
  nextChange: number;
  usedAddresses: string[];
}

/** BIP44 gap-limit scan of receive (0) and change (1) chains. */
export async function scanAccount(k: XpubKey, scheme: Scheme, net: Net, gap = GAP_LIMIT): Promise<BtcAccountState> {
  const state: BtcAccountState = { balance: 0n, utxos: [], nextReceive: 0, nextChange: 0, usedAddresses: [] };
  for (const change of [0, 1]) {
    let index = 0;
    let unused = 0;
    while (unused < gap) {
      const batch = Array.from({ length: 5 }, (_, i) => index + i);
      const stats = await pool(batch, 5, async (i) => {
        const a = scriptFor(scheme, net, derivePubkey(k, change, i).pubkey).address;
        return { i, a, s: await ejson<AddrStats>(net, `/address/${a}`) };
      });
      for (const { i, a, s } of stats) {
        const used = s.chain_stats.tx_count + s.mempool_stats.tx_count > 0;
        if (!used) {
          unused++;
          continue;
        }
        unused = 0;
        state.usedAddresses.push(a);
        if (change === 0) state.nextReceive = i + 1;
        else state.nextChange = i + 1;
        const bal =
          s.chain_stats.funded_txo_sum - s.chain_stats.spent_txo_sum + s.mempool_stats.funded_txo_sum - s.mempool_stats.spent_txo_sum;
        if (bal > 0) {
          const utxos = await ejson<EsploraUtxo[]>(net, `/address/${a}/utxo`);
          for (const u of utxos)
            state.utxos.push({ txid: u.txid, vout: u.vout, value: BigInt(u.value), confirmed: u.status.confirmed, change, index: i, address: a });
        }
      }
      index += batch.length;
    }
  }
  state.balance = state.utxos.reduce((s, u) => s + u.value, 0n);
  return state;
}

export async function history(net: Net, addresses: string[]): Promise<TxRecord[]> {
  const mine = new Set(addresses);
  const seen = new Map<string, TxRecord>();
  await pool(addresses.slice(0, 20), 3, async (a) => {
    const txs = await ejson<
      { txid: string; status: { confirmed: boolean; block_time?: number }; vin: { prevout?: { scriptpubkey_address?: string; value: number } }[]; vout: { scriptpubkey_address?: string; value: number }[] }[]
    >(net, `/address/${a}/txs`);
    for (const t of txs) {
      if (seen.has(t.txid)) continue;
      const inSum = t.vin.filter((v) => v.prevout && mine.has(v.prevout.scriptpubkey_address ?? '')).reduce((s, v) => s + v.prevout!.value, 0);
      const outSum = t.vout.filter((v) => mine.has(v.scriptpubkey_address ?? '')).reduce((s, v) => s + v.value, 0);
      const net_ = outSum - inSum;
      seen.set(t.txid, {
        chain: 'btc',
        hash: t.txid,
        time: t.status.block_time,
        direction: net_ > 0 ? 'in' : net_ < 0 ? 'out' : 'self',
        amount: `${(Math.abs(net_) / 1e8).toFixed(8)} BTC`,
        status: t.status.confirmed ? 'confirmed' : 'pending',
      });
    }
  });
  return [...seen.values()].sort((a, b) => (b.time ?? Infinity) - (a.time ?? Infinity));
}

export const MAX_FEE_RATE = 500; // sat/vB: never suggest more (testnet estimators can be absurd)

/** Plain Esplora /fee-estimates (target blocks -> sat/vB): the estimate for the nearest target at
 *  or above `blocks` (later = cheaper), rounded up, 1..MAX_FEE_RATE. */
export function rateFromEstimates(est: Record<string, number>, blocks: number): number {
  const targets = Object.keys(est).map(Number).filter((n) => n >= blocks && Number.isFinite(est[String(n)])).sort((a, b) => a - b);
  const v = targets.length ? est[String(targets[0])]! : 1;
  return Math.min(MAX_FEE_RATE, Math.max(1, Math.ceil(v)));
}

export async function feeRates(net: Net) {
  return esplora(net, async (b) => {
    try {
      const f = await getJson<{ fastestFee: number; halfHourFee: number; hourFee: number; minimumFee: number }>(`${b}/v1/fees/recommended`, TIMEOUT_MS);
      const cap = (n: number) => Math.min(MAX_FEE_RATE, Math.max(1, n));
      return { fast: cap(f.fastestFee), normal: cap(f.halfHourFee), slow: cap(Math.max(f.hourFee, f.minimumFee)) };
    } catch (e) {
      if (!(e instanceof HttpError && e.status === 404)) throw e; // mempool.space-only endpoint
    }
    const est = await getJson<Record<string, number>>(`${b}/fee-estimates`, TIMEOUT_MS);
    const slow = rateFromEstimates(est, 6);
    const normal = Math.max(slow, rateFromEstimates(est, 3));
    return { fast: Math.max(normal, rateFromEstimates(est, 1)), normal, slow };
  });
}

// ---------------------------------------------------------------- PSBT building

const INPUT_VB: Record<string, number> = { 'btc-p2pkh': 148, 'btc-p2sh-p2wpkh': 91, 'btc-p2wpkh': 68, 'btc-p2tr': 58 };

export function outputVbytes(addr: string, network: Network) {
  return 9 + baddress.toOutputScript(addr, network).length;
}

export function estimateVbytes(scheme: Scheme, inputs: number, outputs: string[], network: Network) {
  return 11 + inputs * INPUT_VB[scheme]! + outputs.reduce((s, a) => s + outputVbytes(a, network), 0);
}

export interface BtcSendPlan {
  inputs: Utxo[];
  outputs: { address: string; value: bigint; change?: { index: number } }[];
  fee: bigint;
  vbytes: number;
}

/**
 * Coin selection (largest first). `amount === 'max'` sweeps every UTXO to `to`.
 * Throws if funds don't cover amount + fee.
 */
export function planSend(args: {
  scheme: Scheme;
  net: Net;
  utxos: Utxo[];
  to: string;
  amount: bigint | 'max';
  feeRate: number; // sat/vB
  changeAddress: string;
  changeIndex: number;
}): BtcSendPlan {
  const network = btcNetwork(args.net);
  baddress.toOutputScript(args.to, network); // validates recipient for this network
  const sorted = [...args.utxos].sort((a, b) => (b.value > a.value ? 1 : b.value < a.value ? -1 : 0));
  const fee = (n: number, outs: string[]) => BigInt(Math.ceil(estimateVbytes(args.scheme, n, outs, network) * args.feeRate));

  if (args.amount === 'max') {
    const total = sorted.reduce((s, u) => s + u.value, 0n);
    const f = fee(sorted.length, [args.to]);
    if (total - f < DUST) throw new Error('Balance too small to cover the network fee');
    return { inputs: sorted, outputs: [{ address: args.to, value: total - f }], fee: f, vbytes: estimateVbytes(args.scheme, sorted.length, [args.to], network) };
  }
  if (args.amount < DUST) throw new Error(`Amount below dust limit (${DUST} sats)`);
  const picked: Utxo[] = [];
  let sum = 0n;
  for (const u of sorted) {
    picked.push(u);
    sum += u.value;
    const withChange = fee(picked.length, [args.to, args.changeAddress]);
    if (sum >= args.amount + withChange) {
      const change = sum - args.amount - withChange;
      if (change >= DUST)
        return {
          inputs: picked,
          outputs: [
            { address: args.to, value: args.amount },
            { address: args.changeAddress, value: change, change: { index: args.changeIndex } },
          ],
          fee: withChange,
          vbytes: estimateVbytes(args.scheme, picked.length, [args.to, args.changeAddress], network),
        };
      // change would be dust: give it to the fee instead
      return { inputs: picked, outputs: [{ address: args.to, value: args.amount }], fee: sum - args.amount, vbytes: estimateVbytes(args.scheme, picked.length, [args.to], network) };
    }
  }
  throw new Error('Insufficient funds for amount + network fee');
}

const fpBytes = (fp: number) => {
  const b = new Uint8Array(4);
  new DataView(b.buffer).setUint32(0, fp >>> 0);
  return b;
};

/** Build the unsigned PSBT the device will check and sign. */
export async function buildPsbt(k: XpubKey, scheme: Scheme, net: Net, plan: BtcSendPlan, prevTx: (txid: string) => Promise<Uint8Array> = fetchTx(net)): Promise<Psbt> {
  const network = btcNetwork(net);
  const psbt = new Psbt({ network });
  const mfp = fpBytes(k.masterFingerprint);
  for (const u of plan.inputs) {
    const { pubkey } = derivePubkey(k, u.change, u.index);
    const s = scriptFor(scheme, net, pubkey);
    const path = fullPath(k, u.change, u.index);
    const base = { hash: u.txid, index: u.vout };
    if (scheme === 'btc-p2pkh') {
      psbt.addInput({ ...base, nonWitnessUtxo: await prevTx(u.txid), bip32Derivation: [{ masterFingerprint: mfp, pubkey, path }] });
    } else if (scheme === 'btc-p2tr') {
      psbt.addInput({
        ...base,
        witnessUtxo: { script: s.output, value: u.value },
        tapInternalKey: s.internalKey!,
        tapBip32Derivation: [{ masterFingerprint: mfp, pubkey: s.internalKey!, path, leafHashes: [] }],
      });
    } else {
      psbt.addInput({
        ...base,
        witnessUtxo: { script: s.output, value: u.value },
        ...(s.redeemScript ? { redeemScript: s.redeemScript } : {}),
        bip32Derivation: [{ masterFingerprint: mfp, pubkey, path }],
      });
    }
  }
  for (const o of plan.outputs) {
    if (o.change) {
      // Key origin lets the DEVICE verify this output is really its own change.
      const { pubkey } = derivePubkey(k, 1, o.change.index);
      const s = scriptFor(scheme, net, pubkey);
      if (s.address !== o.address) throw new Error('change address mismatch');
      const path = fullPath(k, 1, o.change.index);
      psbt.addOutput({
        address: o.address,
        value: o.value,
        ...(scheme === 'btc-p2tr'
          ? { tapInternalKey: s.internalKey!, tapBip32Derivation: [{ masterFingerprint: mfp, pubkey: s.internalKey!, path, leafHashes: [] }] }
          : { bip32Derivation: [{ masterFingerprint: mfp, pubkey, path }], ...(s.redeemScript ? { redeemScript: s.redeemScript } : {}) }),
      });
    } else {
      psbt.addOutput({ address: o.address, value: o.value });
    }
  }
  return psbt;
}

const fetchTx = (net: Net) => async (txid: string) => hexToBytes(await esplora(net, (b) => getText(`${b}/tx/${txid}/hex`)));

/** Verify the device's signatures, finalise, and return raw tx hex. */
export function finalizeSigned(signed: Uint8Array, net: Net, expectedUnsigned: Psbt): string {
  const psbt = Psbt.fromBuffer(signed, { network: btcNetwork(net) });
  // The device must not have changed what we asked it to sign.
  if (bytesToHex(psbt.data.globalMap.unsignedTx.toBuffer()) !== bytesToHex(expectedUnsigned.data.globalMap.unsignedTx.toBuffer()))
    throw new Error('Signed PSBT does not match the transaction that was sent for signing');
  const ok = psbt.validateSignaturesOfAllInputs((pub, msg, sig) =>
    sig.length === 64 && pub.length === 32 ? ecc.verifySchnorr(msg, pub, sig) : ecc.verify(msg, pub, sig),
  );
  if (!ok) throw new Error('Invalid signature from device');
  psbt.finalizeAllInputs();
  return psbt.extractTransaction().toHex();
}

export const broadcast = (net: Net, hex: string) => esplora(net, (b) => postText(`${b}/tx`, hex));

export async function txConfirmed(net: Net, txid: string): Promise<boolean> {
  const s = await ejson<{ confirmed: boolean }>(net, `/tx/${txid}/status`);
  return s.confirmed;
}
