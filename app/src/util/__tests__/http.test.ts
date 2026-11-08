import { CHAINS } from '../../chains/config';
import { HttpError, friendlyError, getJson } from '../http';

describe('HTTP errors shown to the user', () => {
  const realFetch = globalThis.fetch;
  afterEach(() => {
    globalThis.fetch = realFetch;
  });

  it('never shows a raw HTML page (bot protection / web filter)', async () => {
    globalThis.fetch = jest.fn(async () => new Response('<!DOCTYPE html><html><head><title>Just a moment...</title>', { status: 403 })) as typeof fetch;
    const e = (await getJson('https://base.blockscout.com/api/v2/x').catch((x: unknown) => x)) as Error;
    expect(e).toBeInstanceOf(HttpError);
    expect(e.message).toMatch(/^HTTP 403: base\.blockscout\.com sent a web page instead of data/);
    expect(e.message).not.toMatch(/<|DOCTYPE/);
    expect(friendlyError(e)).toBe(e.message);
  });

  it('keeps a short JSON error body with the host', async () => {
    globalThis.fetch = jest.fn(async () => new Response('{"error":"Invalid Bitcoin address"}', { status: 400 })) as typeof fetch;
    const e = (await getJson('https://mempool.space/testnet4/api/address/x').catch((x: unknown) => x)) as Error;
    expect(e.message).toBe('HTTP 400 from mempool.space: {"error":"Invalid Bitcoin address"}');
  });

  it('turns network-level failures into one plain line', () => {
    for (const m of ['Network request failed', 'TypeError: fetch failed', 'HTTP request failed.\nURL: https://x\nDetails: SSL error', 'The request timed out.'])
      expect(friendlyError(new Error(m))).toBe('could not reach the service (offline, or blocked by a web filter on this network)');
    expect(friendlyError(new Error('429 Too Many Requests'))).toMatch(/rate-limiting/);
    expect(friendlyError(new Error('x'.repeat(300))).length).toBeLessThanOrEqual(141);
  });
});

describe('RPC configuration', () => {
  it('every EVM network has an https RPC, and none on a plain-HTTP port', () => {
    for (const c of Object.values(CHAINS).filter((x) => x.family === 'evm'))
      for (const n of Object.values(c.net))
        for (const u of [n.rpc!, ...(n.rpcFallbacks ?? [])]) {
          expect(u).toMatch(/^https:\/\//);
          expect(u).not.toMatch(/:8545\b/); // https on BNB's plain-HTTP port failed with a TLS error
        }
  });
});
