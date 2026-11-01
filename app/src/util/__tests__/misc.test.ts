import type { Holding } from '../../chains/types';
import { applyPrices, clearPriceCache, priceHoldings } from '../../prices/prices';
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

  beforeEach(() => clearPriceCache());

  it('uses CoinGecko, falls back to DexScreener (liquid pairs only), skips testnet', async () => {
    const calls: string[] = [];
    const fake = async <T,>(url: string): Promise<T> => {
      calls.push(url);
      if (url.includes('simple/price')) return { ethereum: { usd: 3000 } } as T;
      if (url.includes('simple/token_price')) return {} as T; // CoinGecko doesn't know the memecoin
      if (url.includes('dexscreener'))
        return [
          { baseToken: { address: '0xMEME' }, priceUsd: '0.5', liquidity: { usd: 50_000 } },
          { baseToken: { address: '0xMEME' }, priceUsd: '9.9', liquidity: { usd: 10 } }, // illiquid: ignored
          { baseToken: { address: '0xRUG' }, priceUsd: '1', liquidity: { usd: 100 } }, // below floor
        ] as T;
      throw new Error(url);
    };
    const holdings = [
      h({}),
      h({ asset: { kind: 'token', address: '0xMEME', standard: 'erc20' }, kind: 'token', symbol: 'MEME', amount: 10n ** 18n }),
      h({ asset: { kind: 'token', address: '0xRUG', standard: 'erc20' }, kind: 'token', symbol: 'RUG' }),
      h({ net: 'test' }),
    ];
    const prices = await priceHoldings(holdings, fake);
    const priced = applyPrices(holdings, prices);
    expect(priced[0]!.usdValue).toBe(6000);
    expect(priced[1]!.usdValue).toBe(0.5);
    expect(priced[2]!.usdValue).toBeUndefined();
    expect(prices.get('eth:0xmeme')?.source).toBe('dexscreener');
    expect(calls.some((c) => c.includes('test'))).toBe(false);
  });
});
