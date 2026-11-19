/*
 * Splitting a wallet's holdings so the screens show what matters first:
 *   coins       native coins (BTC, ETH on each network, SOL, ...) and staked coins
 *   tokens      tokens with a verified identity: the bundled hand-checked list or the curated
 *               token lists (logoStore.ts), matched by contract address
 *   unverified  everything else: airdrops, test tokens, look-alikes ("Tether USD" from a random
 *               deployer), NFTs and LP tokens. Shown folded away with a warning.
 * Zero balances are dropped (every EVM network reports a native balance, usually 0).
 */
import type { Holding } from '../chains/types';
import { assetLogo, assetSeed } from './coinLogos';
import { downloadedLogo } from './logoStore';

type H = Pick<Holding, 'chain' | 'asset' | 'kind' | 'amount' | 'symbol'> & { usdValue?: number; usd?: number };

export function isVerified(h: Pick<Holding, 'chain' | 'asset'>): boolean {
  if (h.asset.kind === 'native') return true;
  const seed = assetSeed(h);
  return !!assetLogo(h) || (!!seed && !!downloadedLogo(seed));
}

const ORDER = ['BTC', 'ETH', 'SOL', 'BNB', 'POL', 'MATIC'];
const usdOf = (h: H) => h.usdValue ?? h.usd ?? 0;

export function splitHoldings<T extends H>(xs: T[]): { coins: T[]; tokens: T[]; unverified: T[] } {
  const coins: T[] = [], tokens: T[] = [], unverified: T[] = [];
  for (const h of xs) {
    if (h.amount <= 0n) continue;
    if (h.kind === 'native' || h.kind === 'staked') coins.push(h);
    else if (h.kind === 'token' && isVerified(h)) tokens.push(h);
    else unverified.push(h);
  }
  const rank = (s: string) => (ORDER.indexOf(s) < 0 ? 99 : ORDER.indexOf(s));
  coins.sort((a, b) => usdOf(b) - usdOf(a) || rank(a.symbol) - rank(b.symbol));
  tokens.sort((a, b) => usdOf(b) - usdOf(a) || a.symbol.localeCompare(b.symbol));
  unverified.sort((a, b) => a.symbol.localeCompare(b.symbol));
  return { coins, tokens, unverified };
}
