import type { Holding } from '../../chains/types';
import { assetKey } from '../../chains/types';
import { applyPrices, clearPriceCache, decodePythAccount, priceHoldings, type Sources } from '../../prices/prices';
import { formatAmount, formatUsd, parseAmount } from '../amount';

describe('amount', () => {
  it('parses exactly, no floats', () => {
    expect(parseAmount('0.1', 18)).toBe(100_000_000_000_000_000n);
    expect(parseAmount('1,234.5', 8)).toBe(123_450_000_000n);
    expect(parseAmount('.5', 9)).toBe(500_000_000n);
    expect(parseAmount('0.000000001', 9)).toBe(1n);
    expect(() => parseAmount('0.0000000001', 9)).toThrow(/decimal/);
    expect(() => parseAmount('1e5', 9)).toThrow();
    expect(() => parseAmount('-1', 9)).toThrow();
    expect(() => parseAmount('', 9)).toThrow();
  });
  it('formats', () => {
    expect(formatAmount(123_450_000_000n, 8)).toBe('1,234.5');
    expect(formatAmount(1n, 18, 6)).toBe('0');
    expect(formatAmount(0n, 9)).toBe('0');
    expect(formatUsd(0.004)).toBe('< $0.01');
    expect(formatUsd(undefined)).toBe('—');
  });
});

describe('prices', () => {
  const h = (p: Partial<Holding>): Holding => ({
    chain: 'eth', net: 'main', scheme: 'evm-bip44', account: 0, owner: '0x', asset: { kind: 'native' }, kind: 'native',
    symbol: 'ETH', name: 'Ethereum', decimals: 18, amount: 2_000_000_000_000_000_000n, ...p,
  });
  const tok = (chain: Holding['chain'], address: string, symbol: string, decimals = 18, amount = 10n ** 18n) =>
    h({ chain, asset: { kind: 'token', address, standard: chain === 'sol' ? 'spl' : 'erc20' }, kind: 'token', symbol, decimals, amount });
  const src = (o: Partial<Sources> = {}): Sources & { calls: string[] } => {
    const calls: string[] = [];
    return {
      calls,
      coinbase: async () => (calls.push('cb'), { ETH: 3000, SOL: 150, PEPE: 0.00001, ARB: 0.5 }),
      kraken: async () => (calls.push('kr'), { XETHZUSD: 2990, PEPEUSD: 0.000011, ARBUSD: 0.51, BONKUSD: 0.00002, JUPUSD: 0.27 }),
      pyth: async () => (calls.push('py'), { SOL: 149, JUP: 0.26 }),
      chainlink: async () => (calls.push('cl'), { ETH: 2980, BTC: 60000, WBTC_BTC: 1 }),
      dexscreener: async (_c, a) => (calls.push('dex:' + a.join(',')), Object.fromEntries(a.map((x) => [x, 0.5]))),
      ...o,
    };
  };

  beforeEach(() => clearPriceCache());

  it('big coins from Coinbase, other listed tokens from Kraken, Solana tokens from Pyth', async () => {
    const s = src();
    const holdings = [
      h({}), // ETH: Coinbase
      tok('eth', '0x6982508145454ce325ddbe47a25d4ec3d2311933', 'PEPE'), // listed: Kraken first
      tok('sol', 'JUPyiwrYJFskUPiHa7hkeR8VUtAeFoSYbKedZNsDvCN', 'JUP', 6, 1_000_000n), // Pyth first
      tok('sol', 'DezXAZ8z7PnrnRJjz3wXBoRgixCa6xjnB7YaB1pPB263', 'BONK', 5, 100_000n), // Kraken
    ];
    const prices = await priceHoldings(holdings, s);
    const q = holdings.map((x) => prices.get(assetKey(x)));
    expect(q.map((x) => x?.source)).toEqual(['coinbase', 'kraken', 'pyth', 'kraken']);
    expect(q.map((x) => x?.usd)).toEqual([3000, 0.000011, 0.26, 0.00002]);
    expect(s.calls.sort()).toEqual(['cb', 'kr', 'py']); // no Chainlink, no DexScreener needed
  });

  it('Coinbase prices big coins only: a listed token gets no Coinbase price when Kraken is down', async () => {
    const s = src({ kraken: async () => Promise.reject(new TypeError('Network request failed')) });
    const pepe = tok('eth', '0x6982508145454ce325ddbe47a25d4ec3d2311933', 'PEPE'); // Coinbase has PEPE too
    const prices = await priceHoldings([h({}), pepe], s);
    expect(prices.get('eth:native')?.source).toBe('coinbase');
    expect(prices.get(assetKey(pepe))).toBeUndefined();
  });

  it('never by name: a fake token called PEPE is a meme coin for DexScreener, testnet gets nothing', async () => {
    const s = src();
    const fake = tok('eth', '0x000000000000000000000000000000000000fa4e', 'PEPE');
    const holdings = [fake, h({ net: 'test' })];
    const priced = applyPrices(holdings, await priceHoldings(holdings, s));
    expect(priced[0]!.usdValue).toBe(0.5);
    expect(priced[1]!.usdValue).toBeUndefined();
    expect(s.calls).toContain('dex:0x000000000000000000000000000000000000fa4e');
  });

  it('falls back: Kraken when Coinbase is down; Chainlink only when both exchanges are', async () => {
    const down = async () => Promise.reject(new TypeError('Network request failed'));
    let s = src({ coinbase: down });
    expect((await priceHoldings([h({})], s)).get('eth:native')).toEqual({ usd: 2990, source: 'kraken' });
    expect(s.calls).not.toContain('cl');
    clearPriceCache();
    s = src({ coinbase: down, kraken: down });
    expect((await priceHoldings([h({})], s)).get('eth:native')).toEqual({ usd: 2980, source: 'chainlink' });
  });

  it('reads the fixed lists at most every 15 s; testnet alone makes no request', async () => {
    const s = src();
    await priceHoldings([h({})], s);
    await priceHoldings([h({})], s);
    expect(s.calls.filter((c) => c === 'cb')).toHaveLength(1);
    clearPriceCache();
    const t = src();
    expect((await priceHoldings([h({ net: 'test' })], t)).size).toBe(0);
    expect(t.calls).toHaveLength(0);
  });

  it('decodes a Pyth price account, rejecting another feed, old prices and wide uncertainty', () => {
    const id = 'ef0d8b6fda2ceba41da15d4095d1da392a0d2f8ed0c6c7bc0f4cfac8c280b56d';
    const acct = (price: bigint, conf: bigint, ageS: number, feed = id) => {
      const b = new Uint8Array(134);
      const v = new DataView(b.buffer);
      b[40] = 1; // verification level Full
      b.set(Uint8Array.from(feed.match(/../g)!, (x) => parseInt(x, 16)), 41);
      v.setBigInt64(73, price, true);
      v.setBigUint64(81, conf, true);
      v.setInt32(89, -8, true);
      v.setBigInt64(93, BigInt(Math.floor(1_000_000 - ageS)), true);
      return b;
    };
    expect(decodePythAccount(acct(7_500_000_000n, 3_000_000n, 30), id, 1_000_000)).toBe(75);
    expect(decodePythAccount(acct(7_500_000_000n, 3_000_000n, 30, 'aa'.repeat(32)), id, 1_000_000)).toBeUndefined();
    expect(decodePythAccount(acct(7_500_000_000n, 3_000_000n, 3600), id, 1_000_000)).toBeUndefined();
    expect(decodePythAccount(acct(7_500_000_000n, 500_000_000n, 30), id, 1_000_000)).toBeUndefined();
  });
});
