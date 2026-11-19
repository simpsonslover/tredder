// Esplora server fallback and fee estimates (fetch mocked: no network).
import { _test, esplora, feeRates, rateFromEstimates } from '../btc';
import { HttpError } from '../../util/http';

const MEMPOOL = 'https://mempool.space/testnet4/api';
const BLOCKSTREAM = 'https://blockstream.info/testnet4/api';

_test.RETRY_429_MS.splice(0, 2, 1, 1); // no real waiting in tests

describe('esplora fallback', () => {
  it('moves on when the first server is unreachable, and then prefers the one that worked', async () => {
    const tried: string[] = [];
    const r = await esplora('test', async (b) => {
      tried.push(b);
      if (b === MEMPOOL) throw new TypeError('Network request failed');
      return 'ok';
    });
    expect(r).toBe('ok');
    expect(tried).toEqual([MEMPOOL, BLOCKSTREAM]);
    tried.length = 0;
    await esplora('test', async (b) => (tried.push(b), 'ok'));
    expect(tried).toEqual([BLOCKSTREAM]); // no second wait on the dead server
  });

  it('waits out a rate limit on the same server before moving on', async () => {
    const tried: string[] = [];
    await esplora('main', async (b) => {
      tried.push(b);
      if (tried.length <= 2) throw new HttpError(429, 'slow down');
      return 1;
    });
    expect(new Set(tried).size).toBe(1);
    expect(tried).toHaveLength(3);
  });

  it('never has more than 4 requests in flight on one server', async () => {
    let now = 0;
    let peak = 0;
    await Promise.all(
      Array.from({ length: 20 }, () =>
        esplora('main', async () => {
          peak = Math.max(peak, ++now);
          await new Promise((r) => setTimeout(r, 2));
          now--;
        }),
      ),
    );
    expect(peak).toBe(4);
  });

  it('moves on for bot pages and server errors, but a real answer (404) is final', async () => {
    for (const status of [403, 503]) {
      const tried: string[] = [];
      await esplora('main', async (b) => {
        tried.push(b);
        if (tried.length === 1) throw new HttpError(status, 'x');
        return 1;
      });
      expect(tried).toHaveLength(2);
    }
    const tried: string[] = [];
    await expect(
      esplora('main', async (b) => {
        tried.push(b);
        throw new HttpError(404, 'Transaction not found');
      }),
    ).rejects.toThrow('Transaction not found');
    expect(tried).toHaveLength(1);
  });

  it('reports the last error when every server fails', async () => {
    await expect(esplora('main', async () => Promise.reject(new TypeError('Network request failed')))).rejects.toThrow('Network request failed');
  });
});

describe('fee estimates', () => {
  it('uses the nearest target at or above, rounds up, stays within 1..500 sat/vB', () => {
    const est = { '1': 1.07, '3': 1.07, '6': 0.125, '144': 0.112 };
    expect(rateFromEstimates(est, 1)).toBe(2);
    expect(rateFromEstimates(est, 6)).toBe(1);
    // Blockstream testnet4 (measured 2026-11-16): only far targets, some absurd
    const t4 = { '144': 0.495, '1008': 361.706, '504': 594214.209 };
    expect(rateFromEstimates(t4, 1)).toBe(1);
    expect(rateFromEstimates({ '2': 594214 }, 1)).toBe(500);
    expect(rateFromEstimates({}, 1)).toBe(1);
  });

  it('falls back from mempool.space fees to plain Esplora /fee-estimates', async () => {
    const fetchMock = jest.spyOn(globalThis, 'fetch').mockImplementation(async (url: RequestInfo | URL) => {
      const u = String(url);
      if (u.startsWith('https://mempool.space')) throw new TypeError('Network request failed');
      if (u.endsWith('/v1/fees/recommended')) return new Response('endpoint does not exist', { status: 404 });
      if (u.endsWith('/fee-estimates')) return new Response(JSON.stringify({ '1': 12.2, '3': 8, '6': 3.5 }), { status: 200 });
      throw new Error('unexpected ' + u);
    });
    try {
      expect(await feeRates('main')).toEqual({ fast: 13, normal: 8, slow: 4 });
    } finally {
      fetchMock.mockRestore();
    }
  });
});
