import { MockTransport } from '../../transport/mock';
import type { Transport } from '../../transport/types';
import { DeviceClient, validate } from '../client';
import { encodeMessage, parseIncoming } from '../codec';
import { MAX_MESSAGE, Reassembler, toFrames } from '../framing';
import { DeviceError } from '../types';

const bytes = (n: number) => Uint8Array.from({ length: n }, (_, i) => (i * 31 + 7) & 0xff);

describe('framing', () => {
  it.each([0, 1, 15, 16, 17, 100, 5000, 70_000])('round-trips %d bytes at MTU 20 and 244', (n) => {
    for (const size of [20, 244]) {
      const msg = bytes(n);
      const r = new Reassembler();
      const frames = toFrames(msg, size);
      frames.forEach((f) => expect(f.length).toBeLessThanOrEqual(size));
      let out: Uint8Array | null = null;
      frames.forEach((f, i) => {
        out = r.push(f);
        if (i < frames.length - 1) expect(out).toBeNull();
      });
      expect(out).toEqual(msg);
    }
  });

  it('wraps the sequence number past 255', () => {
    const msg = bytes(20 * 300);
    const r = new Reassembler();
    let out: Uint8Array | null = null;
    for (const f of toFrames(msg, 20)) out = r.push(f);
    expect(out).toEqual(msg);
  });

  it('rejects violations and resets', () => {
    const frames = toFrames(bytes(100), 20);
    const r = new Reassembler();
    expect(() => r.push(frames[1]!)).toThrow(/without first/);
    r.push(frames[0]!);
    expect(() => r.push(frames[2]!)).toThrow(/sequence gap/);
    // after a violation, a fresh message works
    let out: Uint8Array | null = null;
    for (const f of frames) out = r.push(f);
    expect(out).toEqual(bytes(100));
  });

  it('rejects oversized declarations and early LAST', () => {
    const r = new Reassembler();
    const big = new Uint8Array(5);
    big[0] = 0x80;
    new DataView(big.buffer).setUint32(1, MAX_MESSAGE + 1);
    expect(() => r.push(big)).toThrow(/too large/);
    const early = toFrames(bytes(50), 20);
    early[1]![0] = 0x40; // LAST on a middle frame
    r.push(early[0]!);
    expect(() => r.push(early[1]!)).toThrow(/before message complete/);
  });
});

describe('codec', () => {
  it('parses responses and events, maps unknown error codes to INTERNAL', () => {
    expect(parseIncoming(encodeMessage({ id: 3, ok: true, result: { a: 1 } }))).toEqual({ id: 3, ok: true, result: { a: 1 } });
    const e = parseIncoming(encodeMessage({ id: 4, ok: false, error: { code: 'WHAT' as never, message: 'x' } }));
    expect(e).toEqual({ id: 4, ok: false, error: { code: 'INTERNAL', message: 'x' } });
    expect(parseIncoming(encodeMessage({ type: 'event', event: 'awaiting-user', id: 1 }))).toMatchObject({ event: 'awaiting-user' });
    expect(() => parseIncoming(encodeMessage({ type: 'event', event: 'bogus' as never }))).toThrow();
  });

  it('validates results strictly', () => {
    expect(() => validate('get-accounts', { accounts: [{ account: 0, path: 'm', kind: 'xpub', publicKey: new Uint8Array(32), chainCode: new Uint8Array(32), masterFingerprint: 1 }] })).toThrow(/33 bytes/);
    expect(() => validate('sign-evm', { r: '0x1', s: 'nothex', yParity: 0 })).toThrow();
    expect(() => validate('sign-sol', { signature: new Uint8Array(63), publicKey: 'x' })).toThrow();
    expect(() => validate('show-address', { address: 5, confirmed: true })).toThrow();
  });
});

describe('client + mock device (real framing at MTU 20)', () => {
  const mk = (o = {}) => new DeviceClient(new MockTransport({ userDelayMs: 5, ...o }));

  it('get-info', async () => {
    const c = mk();
    expect(await c.request({ type: 'get-info' })).toMatchObject({ model: 'mock', protocol: 1, initialized: true });
    await c.close();
  });

  it('get-accounts returns firmware-exported keys', async () => {
    const c = mk();
    const r = await c.request({ type: 'get-accounts', wallet: 'active', net: 'main', scheme: 'btc-p2wpkh', accounts: [0, 1] });
    expect(r.accounts).toHaveLength(2);
    expect(r.accounts[0]).toMatchObject({ kind: 'xpub', path: "m/84'/0'/0'", masterFingerprint: 0x73c5da0a });
    const e = await c.request({ type: 'get-accounts', wallet: 'active', net: 'test', scheme: 'evm-bip44', accounts: [0] });
    expect(e.accounts[0]).toMatchObject({ kind: 'address', address: '0x9858EfFD232B4033E47d90003D41EC34EcaEda94' });
    await c.close();
  });

  it('show-address waits for the user and reports awaiting-user', async () => {
    const events: string[] = [];
    const c = new DeviceClient(new MockTransport({ userDelayMs: 5 }), (e) => events.push(e.event));
    const r = await c.request({ type: 'show-address', wallet: 'active', net: 'main', scheme: 'sol-bip44-change', account: 0, change: 0, index: 0 });
    expect(r).toEqual({ address: 'HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk', confirmed: true });
    expect(events).toContain('awaiting-user');
    await c.close();
  });

  it('user rejection surfaces as USER_REJECTED', async () => {
    const c = mk({ rejectAll: true });
    await expect(
      c.request({ type: 'show-address', wallet: 'active', net: 'main', scheme: 'btc-p2wpkh', account: 0, change: 0, index: 0 }),
    ).rejects.toMatchObject({ code: 'USER_REJECTED' });
    await c.close();
  });

  it('mock refuses to sign anything', async () => {
    const c = mk();
    await expect(c.request({ type: 'sign-btc', wallet: 'active', net: 'test', psbt: new Uint8Array(10) })).rejects.toMatchObject({ code: 'UNSUPPORTED' });
    await expect(c.request({ type: 'sign-evm', wallet: 'active', scheme: 'evm-bip44', account: 0, chainId: 11155111, tx: new Uint8Array(10) })).rejects.toMatchObject({ code: 'UNSUPPORTED' });
    await expect(c.request({ type: 'sign-sol', wallet: 'active', scheme: 'sol-bip44-change', account: 0, net: 'test', message: new Uint8Array(10) })).rejects.toMatchObject({ code: 'UNSUPPORTED' });
    await c.close();
  });

  it('pending requests fail on disconnect', async () => {
    const t = new MockTransport({ userDelayMs: 10_000 });
    const c = new DeviceClient(t);
    const p = c.request({ type: 'show-address', wallet: 'active', net: 'main', scheme: 'btc-p2wpkh', account: 0, change: 0, index: 0 });
    await t.close();
    await expect(p).rejects.toBeInstanceOf(DeviceError);
  });
});

describe('security rules', () => {
  it('refuses to talk plaintext over real BLE', () => {
    const fakeBle: Transport = {
      kind: 'ble', secure: false, frameSize: 20, name: 'x',
      send: async () => {}, onFrame: () => () => {}, onDisconnect: () => () => {}, close: async () => {},
    };
    expect(() => new DeviceClient(fakeBle)).toThrow(/Secure channel/);
  });

  it('times out silent devices', async () => {
    jest.useFakeTimers();
    const silent: Transport = {
      kind: 'mock', secure: false, frameSize: 20, name: 'silent',
      send: async () => {}, onFrame: () => () => {}, onDisconnect: () => () => {}, close: async () => {},
    };
    const c = new DeviceClient(silent);
    const p = c.request({ type: 'get-info' });
    jest.advanceTimersByTime(16_000);
    await expect(p).rejects.toMatchObject({ code: 'TIMEOUT' });
    jest.useRealTimers();
  });
});
