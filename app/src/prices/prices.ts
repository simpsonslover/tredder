// USD prices: CoinGecko first, DexScreener for anything it doesn't know
// (memecoins on EVM + Solana), Jupiter as a last resort for Solana mints.
// Testnet assets have no price.
import { CHAINS, type ChainId } from '../chains/config';
import { assetKey, type Holding } from '../chains/types';
import { getJson } from '../util/http';

export type PriceSource = 'coingecko' | 'dexscreener' | 'jupiter';

const TTL_MS = 60_000;
const cache = new Map<string, { usd: number; at: number; source: PriceSource }>();

export interface PriceQuote {
  usd: number;
  source: PriceSource;
}

type PriceFetcher = typeof getJson;

export async function priceHoldings(holdings: Holding[], fetchJson: PriceFetcher = getJson): Promise<Map<string, PriceQuote>> {
  const out = new Map<string, PriceQuote>();
  const live = holdings.filter((h) => h.net === 'main' && (h.kind === 'native' || h.kind === 'token' || h.kind === 'staked'));
  const now = Date.now();
  const need = new Map<string, Holding>();
  for (const h of live) {
    const k = assetKey(h);
    const c = cache.get(k);
    if (c && now - c.at < TTL_MS) out.set(k, { usd: c.usd, source: c.source });
    else need.set(k, h);
  }
  if (!need.size) return out;
  const put = (k: string, usd: number, source: PriceSource) => {
    if (!Number.isFinite(usd) || usd < 0) return;
    out.set(k, { usd, source });
    cache.set(k, { usd, at: now, source });
  };

  // 1) CoinGecko native coins
  const natives = [...need.entries()].filter(([, h]) => h.asset.kind === 'native');
  if (natives.length) {
    const ids = [...new Set(natives.map(([, h]) => CHAINS[h.chain].coingeckoId))];
    try {
      const r = await fetchJson<Record<string, { usd?: number }>>(`https://api.coingecko.com/api/v3/simple/price?ids=${ids.join(',')}&vs_currencies=usd`);
      for (const [k, h] of natives) {
        const p = r[CHAINS[h.chain].coingeckoId]?.usd;
        if (p !== undefined) put(k, p, 'coingecko');
      }
    } catch {
      /* fall through: shown as "no price" */
    }
  }

  // 2) CoinGecko tokens per platform
  const tokens = [...need.entries()].filter(([k, h]) => h.asset.kind === 'token' && !out.has(k));
  const byChain = new Map<ChainId, [string, Holding][]>();
  for (const e of tokens) byChain.set(e[1].chain, [...(byChain.get(e[1].chain) ?? []), e]);
  for (const [chain, list] of byChain) {
    const platform = CHAINS[chain].cgPlatform;
    if (!platform) continue;
    const addrs = list.map(([, h]) => (h.asset.kind === 'token' ? h.asset.address : ''));
    try {
      const r = await fetchJson<Record<string, { usd?: number }>>(
        `https://api.coingecko.com/api/v3/simple/token_price/${platform}?contract_addresses=${addrs.join(',')}&vs_currencies=usd`,
      );
      const lower = Object.fromEntries(Object.entries(r).map(([a, v]) => [a.toLowerCase(), v]));
      for (const [k, h] of list) {
        const a = h.asset.kind === 'token' ? h.asset.address : '';
        const p = (r[a] ?? lower[a.toLowerCase()])?.usd;
        if (p !== undefined) put(k, p, 'coingecko');
      }
    } catch {
      /* rate-limited: try DexScreener */
    }
  }

  // 3) DexScreener for the rest (memecoins): most liquid pair's USD price
  const rest = tokens.filter(([k]) => !out.has(k));
  const restByChain = new Map<ChainId, [string, Holding][]>();
  for (const e of rest) restByChain.set(e[1].chain, [...(restByChain.get(e[1].chain) ?? []), e]);
  for (const [chain, list] of restByChain) {
    const dex = CHAINS[chain].dexChain;
    if (!dex) continue;
    for (let i = 0; i < list.length; i += 30) {
      const chunk = list.slice(i, i + 30);
      const addrs = chunk.map(([, h]) => (h.asset.kind === 'token' ? h.asset.address : ''));
      try {
        const pairs = await fetchJson<{ baseToken: { address: string }; priceUsd?: string; liquidity?: { usd?: number } }[]>(
          `https://api.dexscreener.com/tokens/v1/${dex}/${addrs.join(',')}`,
        );
        for (const [k, h] of chunk) {
          const a = h.asset.kind === 'token' ? h.asset.address.toLowerCase() : '';
          const best = pairs
            .filter((p) => p.baseToken.address.toLowerCase() === a && p.priceUsd)
            .sort((x, y) => (y.liquidity?.usd ?? 0) - (x.liquidity?.usd ?? 0))[0];
          if (best && (best.liquidity?.usd ?? 0) >= 1000) put(k, Number(best.priceUsd), 'dexscreener');
        }
      } catch {
        /* ignore */
      }
    }
  }

  // 4) Jupiter for Solana mints still unpriced
  const sol = rest.filter(([k, h]) => h.chain === 'sol' && !out.has(k));
  if (sol.length) {
    try {
      const ids = sol.map(([, h]) => (h.asset.kind === 'token' ? h.asset.address : ''));
      const r = await fetchJson<Record<string, { usdPrice?: number }>>(`https://lite-api.jup.ag/price/v3?ids=${ids.join(',')}`);
      for (const [k, h] of sol) {
        const p = r[h.asset.kind === 'token' ? h.asset.address : '']?.usdPrice;
        if (p !== undefined) put(k, p, 'jupiter');
      }
    } catch {
      /* ignore */
    }
  }
  return out;
}

/** Prices already known (no network): for partial totals while balances are still loading. */
export function cachedPrices(holdings: Holding[]): Map<string, PriceQuote> {
  const out = new Map<string, PriceQuote>();
  const now = Date.now();
  for (const h of holdings) {
    const c = cache.get(assetKey(h));
    if (h.net === 'main' && c && now - c.at < TTL_MS) out.set(assetKey(h), { usd: c.usd, source: c.source });
  }
  return out;
}

export function applyPrices(holdings: Holding[], prices: Map<string, PriceQuote>): Holding[] {
  return holdings.map((h) => {
    const q = prices.get(assetKey(h));
    if (!q) return h;
    const usdValue = (Number(h.amount) / 10 ** h.decimals) * q.usd;
    return { ...h, usdPrice: q.usd, usdValue };
  });
}

export function clearPriceCache() {
  cache.clear();
}
