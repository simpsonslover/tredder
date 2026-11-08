// Wallet model + operations (portfolio, receive, send, scan) on top of the
// device client and chain services. UI-free so it can be tested.
import type { Address } from 'viem';

import { buildPsbt, broadcast as btcBroadcast, feeRates, finalizeSigned, history as btcHistory, planSend, scanAccount, txConfirmed, type BtcAccountState } from '../chains/btc';
import { btcAddress, type XpubKey } from '../chains/btcDerive';
import { CHAINS, CHAIN_LIST, type ChainId, type Family } from '../chains/config';
import { broadcastEvm, evmConfirmed, evmHistory, evmHoldings, prepareEvmTransfer, signedEvmTx, type EvmPrepared } from '../chains/evm';
import { broadcastSol, prepareSolTransfer, solConfirmed, solHistory, solHoldings, type SolPrepared } from '../chains/sol';
import type { Holding, TxRecord } from '../chains/types';
import { applyPrices, priceHoldings } from '../prices/prices';
import { friendlyError } from '../util/http';
import type { DeviceClient } from '../protocol/client';
import type { AccountKey, Net, Scheme, WalletRef } from '../protocol/types';

export interface WalletAccount {
  family: Family;
  scheme: Scheme;
  account: number;
  key: AccountKey;
}

export const familyOf = (s: Scheme): Family => (s.startsWith('btc') ? 'btc' : s.startsWith('evm') ? 'evm' : 'sol');
export const accountId = (a: Pick<WalletAccount, 'scheme' | 'account'>) => `${a.scheme}#${a.account}`;
const addressOf = (a: WalletAccount) => (a.key.kind === 'address' ? a.key.address : '');

/** Accounts shown by default after pairing (user can add more via import scan). */
export const DEFAULT_SCHEMES: Scheme[] = ['btc-p2wpkh', 'btc-p2tr', 'evm-bip44', 'sol-bip44-change'];

export async function fetchAccounts(client: DeviceClient, net: Net, schemes: Scheme[], accounts: number[], wallet: WalletRef = 'active'): Promise<WalletAccount[]> {
  const out: WalletAccount[] = [];
  for (const scheme of schemes) {
    const r = await client.request({ type: 'get-accounts', wallet, net, scheme, accounts });
    for (const key of r.accounts) out.push({ family: familyOf(scheme), scheme, account: key.account, key });
  }
  return out;
}

export interface Portfolio {
  holdings: Holding[];
  btc: Record<string, BtcAccountState>; // by accountId
  errors: string[];
  totalUsd: number;
  updatedAt: number;
}

/** Balances for every account on every chain of its family, priced in USD. */
export async function loadPortfolio(accounts: WalletAccount[], net: Net, opts: { btcGap?: number } = {}): Promise<Portfolio> {
  const holdings: Holding[] = [];
  const btc: Record<string, BtcAccountState> = {};
  const errors: string[] = [];
  const jobs: Promise<void>[] = [];
  for (const a of accounts) {
    if (a.family === 'btc' && a.key.kind === 'xpub') {
      const key = a.key;
      jobs.push(
        scanAccount(key, a.scheme, net, opts.btcGap)
          .then((st) => {
            btc[accountId(a)] = st;
            holdings.push({ chain: 'btc', net, scheme: a.scheme, account: a.account, owner: `${a.scheme} #${a.account}`, asset: { kind: 'native' }, kind: 'native', symbol: 'BTC', name: CHAINS.btc.net[net].label, decimals: 8, amount: st.balance });
          })
          .catch((e) => void errors.push(`Bitcoin (${a.scheme}): ${friendlyError(e)}`)),
      );
    } else if (a.family === 'evm') {
      for (const c of CHAIN_LIST.filter((x) => x.family === 'evm'))
        jobs.push(
          evmHoldings(c.id, net, a.scheme, a.account, addressOf(a) as Address)
            .then((hs) => void holdings.push(...hs))
            .catch((e) => void errors.push(`${c.net[net].label}: ${friendlyError(e)}`)),
        );
    } else if (a.family === 'sol') {
      jobs.push(
        solHoldings(net, a.scheme, a.account, addressOf(a))
          .then((hs) => void holdings.push(...hs))
          .catch((e) => void errors.push(`Solana: ${friendlyError(e)}`)),
      );
    }
  }
  await Promise.all(jobs);
  const priced = applyPrices(holdings, await priceHoldings(holdings));
  priced.sort((x, y) => (y.usdValue ?? 0) - (x.usdValue ?? 0) || x.chain.localeCompare(y.chain));
  return { holdings: priced, btc, errors, totalUsd: priced.reduce((s, h) => s + (h.kind === 'staked' || h.kind === 'nft' ? 0 : h.usdValue ?? 0), 0), updatedAt: Date.now() };
}

export async function loadHistory(accounts: WalletAccount[], net: Net, portfolio: Portfolio | null): Promise<TxRecord[]> {
  const jobs: Promise<TxRecord[]>[] = [];
  for (const a of accounts) {
    if (a.family === 'btc') jobs.push(btcHistory(net, portfolio?.btc[accountId(a)]?.usedAddresses ?? []).catch(() => []));
    else if (a.family === 'evm')
      for (const c of CHAIN_LIST.filter((x) => x.family === 'evm')) jobs.push(evmHistory(c.id, net, addressOf(a)).catch(() => []));
    else jobs.push(solHistory(net, addressOf(a)).catch(() => []));
  }
  const all = (await Promise.all(jobs)).flat();
  const seen = new Set<string>();
  return all.filter((t) => (seen.has(t.chain + t.hash) ? false : (seen.add(t.chain + t.hash), true))).sort((a, b) => (b.time ?? Infinity) - (a.time ?? Infinity));
}

// ---------------------------------------------------------------- receive

/** The receive address this app expects for the account (BTC: next unused). NOT verified:
 *  shown only for comparing with the device screen, never copied or encoded as a QR. */
export function expectedReceiveAddress(a: WalletAccount, net: Net, portfolio: Portfolio | null) {
  const index = a.family === 'btc' ? portfolio?.btc[accountId(a)]?.nextReceive ?? 0 : 0;
  const address = a.key.kind === 'xpub' ? btcAddress(a.key, a.scheme, net, 0, index) : a.key.address;
  return { address, index };
}

/** Ask the device to display the address; only a device-confirmed address is returned. */
export async function verifyReceiveAddress(client: DeviceClient, a: WalletAccount, net: Net, portfolio: Portfolio | null) {
  const { address: expected, index } = expectedReceiveAddress(a, net, portfolio);
  const r = await client.request({ type: 'show-address', wallet: 'active', net, scheme: a.scheme, account: a.account, change: 0, index });
  // The app also derives what it expects and refuses if the device disagrees.
  if (r.address !== expected) throw new Error('Device returned a different address than this account: do not use it.');
  if (!r.confirmed) throw new Error('Not confirmed on the device');
  return { address: r.address, index };
}

// ---------------------------------------------------------------- send

export type Prepared =
  | { family: 'btc'; chain: 'btc'; net: Net; account: WalletAccount; psbt: Awaited<ReturnType<typeof buildPsbt>>; fee: bigint; amount: bigint; to: string }
  | { family: 'evm'; chain: ChainId; account: WalletAccount; p: EvmPrepared; amount: bigint; to: string }
  | { family: 'sol'; chain: 'sol'; account: WalletAccount; p: SolPrepared; amount: bigint; to: string };

export async function prepareSend(args: {
  holding: Holding;
  account: WalletAccount;
  net: Net;
  to: string;
  amount: bigint | 'max';
  portfolio: Portfolio | null;
  feeLevel?: 'slow' | 'normal' | 'fast';
}): Promise<Prepared> {
  const { holding: h, account: a, net } = args;
  if (h.chain === 'btc') {
    const st = args.portfolio?.btc[accountId(a)];
    if (!st || a.key.kind !== 'xpub') throw new Error('Refresh the portfolio first');
    const rate = (await feeRates(net))[args.feeLevel ?? 'normal'];
    const changeAddress = btcAddress(a.key, a.scheme, net, 1, st.nextChange);
    const plan = planSend({ scheme: a.scheme, net, utxos: st.utxos, to: args.to.trim(), amount: args.amount, feeRate: rate, changeAddress, changeIndex: st.nextChange });
    const psbt = await buildPsbt(a.key as XpubKey, a.scheme, net, plan);
    return { family: 'btc', chain: 'btc', net, account: a, psbt, fee: plan.fee, amount: plan.outputs[0]!.value, to: args.to.trim() };
  }
  if (CHAINS[h.chain].family === 'evm') {
    const p = await prepareEvmTransfer({ chain: h.chain, net, from: h.owner as Address, to: args.to.trim(), asset: h.asset, amount: args.amount });
    return { family: 'evm', chain: h.chain, account: a, p, amount: args.amount === 'max' ? p.tx.value ?? 0n : args.amount, to: args.to.trim() };
  }
  const p = await prepareSolTransfer({ net, from: h.owner, to: args.to.trim(), asset: h.asset, amount: args.amount, decimals: h.decimals });
  return { family: 'sol', chain: 'sol', account: a, p, amount: args.amount === 'max' ? h.amount - p.fee : args.amount, to: args.to.trim() };
}

/** Device signs (user confirms on its screen), app verifies and broadcasts. Returns tx hash. */
export async function signAndBroadcast(client: DeviceClient, prep: Prepared, wallet: WalletRef = 'active', planStep?: number): Promise<string> {
  if (prep.family === 'btc') {
    const r = await client.request({ type: 'sign-btc', wallet, net: prep.net, psbt: prep.psbt.toBuffer(), planStep });
    return btcBroadcast(prep.net, finalizeSigned(r.psbt, prep.net, prep.psbt));
  }
  if (prep.family === 'evm') {
    const sig = await client.request({ type: 'sign-evm', wallet, scheme: prep.account.scheme, account: prep.account.account, chainId: prep.p.tx.chainId, tx: prep.p.unsigned, planStep });
    return broadcastEvm(prep.p, signedEvmTx(prep.p, sig));
  }
  const r = await client.request({ type: 'sign-sol', wallet, scheme: prep.account.scheme, account: prep.account.account, net: prep.p.net, message: prep.p.message, planStep });
  return broadcastSol(prep.p, r.signature, r.publicKey);
}

export async function txStatus(chain: ChainId, net: Net, hash: string): Promise<'pending' | 'confirmed' | 'failed'> {
  if (chain === 'btc') return (await txConfirmed(net, hash)) ? 'confirmed' : 'pending';
  if (chain === 'sol') return solConfirmed(net, hash);
  return evmConfirmed(chain, net, hash as `0x${string}`);
}
