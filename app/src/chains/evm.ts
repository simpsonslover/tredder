// EVM chains (Ethereum, Base, BNB, Polygon, Arbitrum): balances, ERC-20 discovery
// (Blockscout, no API key), unsigned EIP-1559 transactions for the device,
// simulation via eth_estimateGas, broadcast.
import {
  createPublicClient,
  encodeFunctionData,
  erc20Abi,
  hexToBytes,
  fallback,
  http,
  isAddress,
  serializeTransaction,
  type Address,
  type Hex,
  type PublicClient,
  type TransactionSerializableEIP1559,
} from 'viem';

import type { Net, Scheme } from '../protocol/types';
import { getJson } from '../util/http';
import { CHAINS, type ChainId } from './config';
import { SimulationError, type Holding, type TxRecord } from './types';

const clients = new Map<string, PublicClient>();

export function evmClient(chain: ChainId, net: Net): PublicClient {
  const key = `${chain}:${net}`;
  let c = clients.get(key);
  if (!c) {
    const cfg = CHAINS[chain].net[net];
    // Several public RPCs per chain: one being down, rate-limited or blocked on this network does
    // not take the chain's balance with it. The chain id that gets signed comes from config, never
    // from an RPC.
    const urls = [cfg.rpc!, ...(cfg.rpcFallbacks ?? [])];
    const transport = urls.length > 1 ? fallback(urls.map((u) => http(u, { timeout: 12_000, retryCount: 0 })), { retryCount: 0 }) : http(urls[0], { timeout: 15_000 });
    c = createPublicClient({ transport }) as PublicClient;
    clients.set(key, c);
  }
  return c;
}

export const chainIdOf = (chain: ChainId, net: Net) => CHAINS[chain].net[net].evmChainId!;

interface BlockscoutTokenBalance {
  value: string;
  token: {
    address_hash?: string;
    address?: string;
    symbol: string | null;
    name: string | null;
    decimals: string | null;
    type: string; // ERC-20 / ERC-721 / ERC-1155 / ERC-404
  };
}

/** Native coin balance (one RPC call: fast). */
export async function evmNative(chain: ChainId, net: Net, scheme: Scheme, account: number, owner: Address): Promise<Holding> {
  const cfg = CHAINS[chain];
  const amount = await evmClient(chain, net).getBalance({ address: owner });
  return { chain, net, scheme, account, owner, asset: { kind: 'native' }, kind: 'native', symbol: cfg.symbol, name: cfg.net[net].label, decimals: cfg.decimals, amount };
}

export async function evmHoldings(chain: ChainId, net: Net, scheme: Scheme, account: number, owner: Address): Promise<Holding[]> {
  const [native, tokens] = await Promise.all([evmNative(chain, net, scheme, account, owner), evmTokens(chain, net, scheme, account, owner)]);
  return [native, ...tokens];
}

/** ERC-20 / NFT balances from the chain's Blockscout indexer (often slow: seconds). Best effort:
 *  no indexer, or the indexer failing, gives an empty list. */
export async function evmTokens(chain: ChainId, net: Net, scheme: Scheme, account: number, owner: Address): Promise<Holding[]> {
  const base = { chain, net, scheme, account, owner };
  const out: Holding[] = [];
  const bs = CHAINS[chain].net[net].blockscout;
  if (!bs) return out; // no indexer: native only (user can add tokens manually later)
  try {
    const toks = await getJson<BlockscoutTokenBalance[]>(`${bs}/api/v2/addresses/${owner}/token-balances`);
    for (const t of toks) {
      const address = t.token.address_hash ?? t.token.address;
      if (!address || BigInt(t.value || '0') === 0n) continue;
      const nft = t.token.type !== 'ERC-20';
      const name = t.token.name ?? 'Unknown token';
      out.push({
        ...base,
        asset: { kind: 'token', address, standard: 'erc20' },
        kind: nft ? 'nft' : looksLikeLp(name, t.token.symbol) ? 'lp' : 'token',
        symbol: t.token.symbol ?? '???',
        name,
        decimals: nft ? 0 : Number(t.token.decimals ?? 18),
        amount: BigInt(t.value),
      });
    }
  } catch {
    // indexer down: the native balance is still shown
  }
  return out;
}

/** Heuristic: LP / vault receipt tokens must not be auto-swept (spec). */
export function looksLikeLp(name: string, symbol: string | null) {
  return /\b(LP|UNI-V[23]|SLP|Cake-LP|LP Token|Vault|Position)\b/i.test(`${name} ${symbol ?? ''}`);
}

export async function evmHistory(chain: ChainId, net: Net, owner: string): Promise<TxRecord[]> {
  const bs = CHAINS[chain].net[net].blockscout;
  if (!bs) return [];
  const r = await getJson<{ items: { hash: string; timestamp: string; from: { hash: string }; to: { hash: string } | null; value: string; status: string | null }[] }>(
    `${bs}/api/v2/addresses/${owner}/transactions`,
  );
  const me = owner.toLowerCase();
  return r.items.map((t) => {
    const from = t.from.hash.toLowerCase() === me;
    const to = t.to?.hash.toLowerCase() === me;
    return {
      chain,
      hash: t.hash,
      time: Math.floor(Date.parse(t.timestamp) / 1000),
      direction: from && to ? 'self' : from ? 'out' : 'in',
      amount: `${formatUnits18(BigInt(t.value))} ${CHAINS[chain].symbol}`,
      status: t.status === 'ok' ? 'confirmed' : t.status === 'error' ? 'failed' : 'pending',
    } satisfies TxRecord;
  });
}

const formatUnits18 = (v: bigint) => (Number(v) / 1e18).toLocaleString('en-US', { maximumFractionDigits: 6 });

export interface EvmPrepared {
  chain: ChainId;
  net: Net;
  tx: TransactionSerializableEIP1559;
  unsigned: Uint8Array; // what the device decodes and signs
  maxFee: bigint; // gas limit * maxFeePerGas (worst case)
}

/**
 * Build + simulate a transfer. `asset` native or ERC-20; `amount: 'max'` sends
 * the whole native balance minus the worst-case fee (used by sweep).
 * Throws SimulationError if eth_estimateGas reverts (e.g. transfer-blocking token).
 */
export async function prepareEvmTransfer(args: {
  chain: ChainId;
  net: Net;
  from: Address;
  to: string;
  asset: Holding['asset'];
  amount: bigint | 'max';
  nonce?: number; // set when queuing several txs (sweep)
  reserveWei?: bigint; // extra native to leave for later txs
}): Promise<EvmPrepared> {
  if (!isAddress(args.to, { strict: false })) throw new Error('Invalid EVM address');
  const c = evmClient(args.chain, args.net);
  const to = args.to as Address;
  const fees = await c.estimateFeesPerGas();
  const nonce = args.nonce ?? (await c.getTransactionCount({ address: args.from, blockTag: 'pending' }));
  let call: { to: Address; value: bigint; data?: Hex };

  if (args.asset.kind === 'token') {
    if (args.amount === 'max') throw new Error('token max must be resolved by caller');
    call = { to: args.asset.address as Address, value: 0n, data: encodeFunctionData({ abi: erc20Abi, functionName: 'transfer', args: [to, args.amount] }) };
  } else {
    call = { to, value: args.amount === 'max' ? 1n : args.amount };
  }

  let gas: bigint;
  try {
    gas = await c.estimateGas({ account: args.from, ...call });
  } catch (e) {
    throw new SimulationError(`Simulation failed: ${(e as Error).message.split('\n')[0]}`);
  }
  gas = (gas * 12n) / 10n; // +20% headroom
  const maxFee = gas * fees.maxFeePerGas;

  if (args.asset.kind === 'native' && args.amount === 'max') {
    const bal = await c.getBalance({ address: args.from });
    const value = bal - maxFee - (args.reserveWei ?? 0n);
    if (value <= 0n) throw new SimulationError('Balance does not cover the network fee');
    call = { to, value };
  }

  const tx: TransactionSerializableEIP1559 = {
    type: 'eip1559',
    chainId: chainIdOf(args.chain, args.net),
    nonce,
    to: call.to,
    value: call.value,
    data: call.data,
    gas,
    maxFeePerGas: fees.maxFeePerGas,
    maxPriorityFeePerGas: fees.maxPriorityFeePerGas,
  };
  return { chain: args.chain, net: args.net, tx, unsigned: hexToBytes(serializeTransaction(tx)), maxFee };
}

export function signedEvmTx(p: EvmPrepared, sig: { r: Hex; s: Hex; yParity: 0 | 1 }): Hex {
  return serializeTransaction(p.tx, sig);
}

export async function broadcastEvm(p: EvmPrepared, raw: Hex): Promise<Hex> {
  return evmClient(p.chain, p.net).sendRawTransaction({ serializedTransaction: raw });
}

export async function evmConfirmed(chain: ChainId, net: Net, hash: Hex): Promise<'pending' | 'confirmed' | 'failed'> {
  const r = await evmClient(chain, net).getTransactionReceipt({ hash }).catch(() => null);
  if (!r) return 'pending';
  return r.status === 'success' ? 'confirmed' : 'failed';
}
