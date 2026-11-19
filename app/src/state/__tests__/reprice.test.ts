// Automatic price updates: only prices change, the balances and their time stay. Every network
// layer is mocked: Coinbase and Kraken (getJson), Pyth (Solana server), Chainlink (Ethereum server).
import type { Holding } from '../../chains/types';
import { clearPriceCache } from '../../prices/prices';
import { KRAKEN_PAIRS } from '../../prices/priceTable';
import { repricePortfolio, type Portfolio } from '../wallet';

const mockGetJson = jest.fn();
const mockAccounts = jest.fn();
const mockMulticall = jest.fn();
jest.mock('../../util/http', () => ({ ...jest.requireActual('../../util/http'), getJson: (url: string) => mockGetJson(url) }));
jest.mock('../../chains/sol', () => ({ ...jest.requireActual('../../chains/sol'), solConnection: () => ({ getMultipleAccountsInfo: mockAccounts }) }));
jest.mock('../../chains/evm', () => ({ ...jest.requireActual('../../chains/evm'), evmClient: () => ({ multicall: mockMulticall }) }));

const eth: Holding = {
  chain: 'eth', net: 'main', scheme: 'evm-bip44', account: 0, owner: '0xabc', asset: { kind: 'native' },
  kind: 'native', symbol: 'ETH', name: 'Ether', decimals: 18, amount: 2n * 10n ** 18n, usdPrice: 3000, usdValue: 6000,
};
const portfolio: Portfolio = { holdings: [eth], btc: {}, errors: [], totalUsd: 6000, updatedAt: 1234 };

function exchanges(cbEth: number | 'down', krEth: number | 'down') {
  mockGetJson.mockImplementation(async (url: string) => {
    if (url.includes('coinbase')) {
      if (cbEth === 'down') throw new TypeError('Network request failed');
      return { data: { rates: { ETH: String(1 / cbEth) } } };
    }
    if (url.includes('kraken')) {
      if (krEth === 'down') throw new TypeError('Network request failed');
      return { error: [], result: { XETHZUSD: { c: [String(krEth)] } } };
    }
    throw new Error('unexpected ' + url);
  });
}

beforeEach(() => {
  mockAccounts.mockResolvedValue([]);
  mockMulticall.mockRejectedValue(new Error('not expected'));
});
afterEach(() => {
  jest.resetAllMocks();
  clearPriceCache();
});

it('updates the price and total, keeps the amounts and the balance time', async () => {
  exchanges(3500, 3490);
  const p = await repricePortfolio(portfolio);
  expect(p.holdings[0]!.amount).toBe(eth.amount);
  expect(p.holdings[0]!.usdPrice).toBeCloseTo(3500, 6); // Coinbase first for big coins
  expect(p.totalUsd).toBeCloseTo(7000, 6);
  expect(p.updatedAt).toBe(1234);
  // fixed lists: Coinbase's full rate table, Kraken's full pair list, whatever the wallet holds
  const urls = mockGetJson.mock.calls.map((c) => String(c[0]));
  expect(urls).toContain('https://api.coinbase.com/v2/exchange-rates?currency=USD');
  expect(urls).toContain(`https://api.kraken.com/0/public/Ticker?pair=${KRAKEN_PAIRS.join(',')}`);
  expect(mockAccounts).toHaveBeenCalledTimes(1); // Pyth: one Solana request
  expect(mockMulticall).not.toHaveBeenCalled();
});

it('keeps the last price when every source is down', async () => {
  exchanges('down', 'down');
  const p = await repricePortfolio(portfolio);
  expect(p.holdings[0]!.usdPrice).toBe(3000);
  expect(p.totalUsd).toBe(6000);
});

it('Chainlink (with the Multicall3 address) only when both exchanges are down', async () => {
  exchanges('down', 'down');
  mockMulticall.mockResolvedValue(
    Array.from({ length: 12 }, (_, i) =>
      i === 1 ? { status: 'success', result: [1n, 350_000_000_000n, 0n, BigInt(Math.floor(Date.now() / 1000 - 60)), 1n] } : { status: 'failure', error: new Error('x') },
    ),
  );
  const p = await repricePortfolio(portfolio);
  expect(p.holdings[0]!.usdPrice).toBe(3500);
  expect(mockMulticall.mock.calls[0][0].multicallAddress).toBe('0xcA11bde05977b3631167028862bE2a173976CA11');
});
