// What the device does with a signing request: decode it, check it belongs to
// this wallet, turn it into review pages, and only sign after the user confirms.
// Mirrors the security rules in protocol/PROTOCOL.md.
import { ed25519 } from '@noble/curves/ed25519.js';
import { secp256k1 } from '@noble/curves/secp256k1.js';
import { base58 } from '@scure/base';
import * as btc from '@scure/btc-signer';
import { VersionedMessage } from '@solana/web3.js';
import { bytesToHex, decodeFunctionData, erc20Abi, formatUnits, keccak256, parseTransaction, serializeTransaction, type Hex } from 'viem';
import { sign as signHash } from 'viem/accounts';

import type { Net, Request, Scheme } from '../../app/src/protocol/types';
import { btcNetwork, btcPayment, schemePath, type WalletKeys } from './keys';

export class Refused extends Error {
  constructor(
    public readonly code: 'INVALID_REQUEST' | 'UNSUPPORTED',
    message: string,
  ) {
    super(message);
  }
}

export interface Page {
  title: string;
  lines: string[];
  tone?: 'normal' | 'warn' | 'danger';
}

export interface Analysis {
  pages: Page[];
  /** Destination(s) for sweep plan-step matching. */
  recipients: string[];
  sign(): Promise<unknown>;
}

const MAX_UINT = 2n ** 256n - 1n;
const EVM_NETS: Record<number, string> = {
  1: 'Ethereum', 11155111: 'Sepolia', 8453: 'Base', 84532: 'Base Sepolia', 56: 'BNB Chain', 97: 'BNB testnet',
  137: 'Polygon', 80002: 'Polygon Amoy', 42161: 'Arbitrum One', 421614: 'Arbitrum Sepolia',
};
const NATIVE: Record<number, string> = { 56: 'BNB', 97: 'tBNB', 137: 'POL', 80002: 'POL' };

const split = (s: string, n = 21) => s.match(new RegExp(`.{1,${n}}`, 'g')) ?? [s];
const sats = (v: bigint) => `${formatUnits(v, 8)} BTC`;

// ---------------------------------------------------------------- Bitcoin
export function analyzeBtc(keys: WalletKeys, net: Net, psbt: Uint8Array): Analysis {
  let tx: btc.Transaction;
  try {
    tx = btc.Transaction.fromPSBT(psbt);
  } catch (e) {
    throw new Refused('INVALID_REQUEST', `bad PSBT: ${(e as Error).message}`);
  }
  const network = btcNetwork(net);
  const toSign: { idx: number; key: Uint8Array }[] = [];
  let inSum = 0n;
  for (let i = 0; i < tx.inputsLength; i++) {
    const inp = tx.getInput(i);
    const der = inp.bip32Derivation?.[0] ?? (inp.tapBip32Derivation?.[0] ? [inp.tapBip32Derivation[0][0], inp.tapBip32Derivation[0][1].der] as const : undefined);
    if (!der) throw new Refused('INVALID_REQUEST', `input ${i}: no key origin`);
    const key = keys.keyForPath(der[1].fingerprint, der[1].path);
    if (!key) throw new Refused('INVALID_REQUEST', `input ${i} does not belong to this wallet`);
    const pub = secp256k1.getPublicKey(key, true);
    const listed = der[0];
    if (bytesToHex(listed.length === 32 ? pub.subarray(1) : pub) !== bytesToHex(listed))
      throw new Refused('INVALID_REQUEST', `input ${i}: key origin does not match its public key`);
    const amount = inp.witnessUtxo?.amount ?? (inp.nonWitnessUtxo && inp.index !== undefined ? inp.nonWitnessUtxo.outputs[inp.index]?.amount : undefined);
    if (amount === undefined) throw new Refused('INVALID_REQUEST', `input ${i}: amount unknown (can't compute fee)`);
    inSum += amount;
    toSign.push({ idx: i, key });
  }

  const pages: Page[] = [];
  const recipients: string[] = [];
  let outSum = 0n;
  let changeSum = 0n;
  for (let i = 0; i < tx.outputsLength; i++) {
    const o = tx.getOutput(i);
    if (!o.script || o.amount === undefined) throw new Refused('INVALID_REQUEST', `output ${i} incomplete`);
    outSum += o.amount;
    const addr = btc.Address(network).encode(btc.OutScript.decode(o.script));
    // Change only if WE can re-derive it: fingerprint ours, path on chain 1, same script.
    const der = o.bip32Derivation?.[0]?.[1] ?? o.tapBip32Derivation?.[0]?.[1].der;
    let isChange = false;
    if (der && der.fingerprint >>> 0 === keys.masterFingerprint >>> 0 && der.path.length === 5 && der.path[3] === 1) {
      const pub = secp256k1.getPublicKey(keys.secpKey(der.path), true);
      for (const s of ['btc-p2pkh', 'btc-p2sh-p2wpkh', 'btc-p2wpkh', 'btc-p2tr'] as Scheme[])
        if (bytesToHex(btcPayment(s, net, pub).script) === bytesToHex(o.script)) isChange = true;
    }
    if (isChange) changeSum += o.amount;
    else {
      recipients.push(addr);
      pages.push({ title: `Send ${pages.length + 1}`, lines: [sats(o.amount), 'to', ...split(addr)] });
    }
  }
  const fee = inSum - outSum;
  if (fee < 0n) throw new Refused('INVALID_REQUEST', 'outputs exceed inputs');
  const sent = outSum - changeSum;
  const high = sent > 0n && fee * 10n > sent;
  pages.push({
    title: 'Fee',
    lines: [sats(fee), ...(changeSum ? [`Change back to you: ${sats(changeSum)} (verified)`] : []), net === 'test' ? 'Bitcoin TESTNET' : 'Bitcoin MAINNET'],
    tone: high ? 'warn' : 'normal',
  });
  if (high) pages.push({ title: 'High fee!', lines: ['The fee is more than 10%', 'of the amount sent.', 'Reject if unexpected.'], tone: 'warn' });
  return {
    pages,
    recipients,
    async sign() {
      for (const { idx, key } of toSign) tx.signIdx(key, idx);
      return { psbt: tx.toPSBT() };
    },
  };
}

// ---------------------------------------------------------------- EVM
export function analyzeEvm(keys: WalletKeys, req: Extract<Request, { type: 'sign-evm' }>, blindSigning: boolean): Analysis {
  let tx: ReturnType<typeof parseTransaction>;
  try {
    tx = parseTransaction(bytesToHex(req.tx));
  } catch (e) {
    throw new Refused('INVALID_REQUEST', `bad transaction: ${(e as Error).message.split('\n')[0]}`);
  }
  if (tx.type !== 'eip1559') throw new Refused('UNSUPPORTED', 'only EIP-1559 transactions');
  if (tx.chainId !== req.chainId) throw new Refused('INVALID_REQUEST', 'chainId mismatch');
  if (!tx.to) throw new Refused('UNSUPPORTED', 'contract deployment not supported');
  const netName = EVM_NETS[req.chainId] ?? `chain ${req.chainId} (unknown!)`;
  const sym = NATIVE[req.chainId] ?? 'ETH';
  const from = keys.evmAddress(req.scheme, req.account);
  const maxFee = (tx.gas ?? 0n) * (tx.maxFeePerGas ?? 0n);
  const pages: Page[] = [{ title: 'Network', lines: [netName, `From account #${req.account}`, ...split(from)] }];
  const recipients: string[] = [];

  if (!tx.data || tx.data === '0x') {
    recipients.push(tx.to);
    pages.push({ title: 'Send', lines: [`${formatUnits(tx.value ?? 0n, 18)} ${sym}`, 'to', ...split(tx.to)] });
  } else {
    let decoded: ReturnType<typeof decodeFunctionData<typeof erc20Abi>> | null = null;
    try {
      decoded = decodeFunctionData({ abi: erc20Abi, data: tx.data });
    } catch {
      decoded = null;
    }
    const hint = req.hints?.tokenDecimals !== undefined ? ` (decimals ${req.hints.tokenDecimals}, unverified)` : '';
    const tok = `${req.hints?.tokenSymbol ?? 'token'} contract`;
    if (decoded?.functionName === 'transfer') {
      const [to, amount] = decoded.args;
      recipients.push(to);
      pages.push({ title: 'Token transfer', lines: [`${amount} base units${hint}`, 'to', ...split(to), tok, ...split(tx.to)] });
    } else if (decoded?.functionName === 'approve') {
      const [spender, amount] = decoded.args;
      const unlimited = amount >= MAX_UINT / 2n;
      pages.push({
        title: unlimited ? '!! UNLIMITED APPROVAL !!' : 'Token approval',
        lines: [unlimited ? 'Spender can take ALL of' : `Allow ${amount} base units`, `this token, forever:`, ...split(spender), tok, ...split(tx.to)],
        tone: unlimited ? 'danger' : 'warn',
      });
    } else if (!blindSigning) {
      throw new Refused('UNSUPPORTED', 'Cannot decode this contract call. Blind signing is disabled on the device.');
    } else {
      pages.push({ title: 'BLIND SIGNING', lines: ['Unknown contract call.', `Data: ${tx.data.slice(0, 18)}…`, 'You cannot verify what', 'this does. Reject unless', 'you are certain.'], tone: 'danger' });
      recipients.push(tx.to);
    }
  }
  pages.push({ title: 'Max fee', lines: [`${formatUnits(maxFee, 18)} ${sym}`, netName] });
  return {
    pages,
    recipients,
    async sign() {
      const key = keys.secpKey(schemePath(req.scheme, 'main', req.account));
      const hash = keccak256(serializeTransaction(tx as Parameters<typeof serializeTransaction>[0]));
      const s = await signHash({ hash, privateKey: bytesToHex(key) as Hex });
      return { r: s.r, s: s.s, yParity: s.yParity as 0 | 1 };
    },
  };
}

// ---------------------------------------------------------------- Solana
const SYSTEM = '11111111111111111111111111111111';
const TOKEN = 'TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA';
const TOKEN22 = 'TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb';
const ATA = 'ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL';
const COMPUTE = 'ComputeBudget111111111111111111111111111111';

export function analyzeSol(keys: WalletKeys, req: Extract<Request, { type: 'sign-sol' }>, blindSigning: boolean): Analysis {
  let msg: VersionedMessage;
  try {
    msg = VersionedMessage.deserialize(req.message);
  } catch (e) {
    throw new Refused('INVALID_REQUEST', `bad message: ${(e as Error).message}`);
  }
  const me = keys.solAddress(req.scheme, req.account);
  const keysList = msg.staticAccountKeys.map((k) => k.toBase58());
  if (keysList[0] !== me) throw new Refused('INVALID_REQUEST', 'fee payer is not this account');
  if (msg.addressTableLookups.length && !blindSigning) throw new Refused('UNSUPPORTED', 'address lookup tables need blind signing');
  const pages: Page[] = [{ title: 'Network', lines: [req.net === 'test' ? 'Solana devnet' : 'Solana MAINNET', `Account #${req.account}`, ...split(me)] }];
  const recipients: string[] = [];
  for (const ix of msg.compiledInstructions) {
    const prog = keysList[ix.programIdIndex];
    const acc = (i: number) => keysList[ix.accountKeyIndexes[i]!] ?? '?';
    const d = ix.data;
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    if (prog === SYSTEM && d.length >= 12 && dv.getUint32(0, true) === 2) {
      recipients.push(acc(1));
      pages.push({ title: 'Send SOL', lines: [`${formatUnits(dv.getBigUint64(4, true), 9)} SOL`, 'to', ...split(acc(1))] });
    } else if ((prog === TOKEN || prog === TOKEN22) && d[0] === 12 && d.length >= 10) {
      const amount = dv.getBigUint64(1, true);
      pages.push({ title: 'Send token', lines: [`${formatUnits(amount, d[9]!)} tokens`, 'mint', ...split(acc(1)), 'to token account', ...split(acc(2))] });
    } else if (prog === ATA) {
      recipients.push(acc(2));
      pages.push({ title: 'Create account', lines: ['Token account for', ...split(acc(2)), 'Costs ~0.002 SOL rent'] });
    } else if (prog === COMPUTE) {
      // priority fee settings: harmless
    } else if (!blindSigning) {
      throw new Refused('UNSUPPORTED', `Cannot decode program ${prog?.slice(0, 8)}…. Blind signing is disabled on the device.`);
    } else {
      pages.push({ title: 'BLIND SIGNING', lines: [`Unknown program`, ...split(prog ?? '?'), 'Reject unless certain.'], tone: 'danger' });
    }
  }
  return {
    pages,
    recipients,
    async sign() {
      const sk = keys.solKey(req.scheme, req.account);
      return { signature: ed25519.sign(req.message, sk), publicKey: base58.encode(ed25519.getPublicKey(sk)) };
    },
  };
}

/** Address equality: EVM hex is case-insensitive (EIP-55 is only a checksum); others exact. */
export const sameAddress = (a: string, b: string) => (a.startsWith('0x') && b.startsWith('0x') ? a.toLowerCase() === b.toLowerCase() : a === b);
