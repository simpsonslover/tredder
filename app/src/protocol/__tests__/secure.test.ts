// The app's whole BLE path without a radio: handshake() + DeviceClient sealing, against a
// stand-in device that runs the responder side of Noise XX the way firmware/app/src/proto.c does
// (msg1 -> msg2, msg3 -> session, then sealed CBOR). Byte compatibility with the real firmware is
// covered by noise.test.ts (shared vectors) and firmware/tools/ble_probe.py (real radio).
import { chacha20poly1305 } from '@noble/ciphers/chacha.js';
import { x25519 } from '@noble/curves/ed25519.js';
import { hmac } from '@noble/hashes/hmac.js';
import { sha256 } from '@noble/hashes/sha2.js';
import { concatBytes, utf8ToBytes } from '@noble/hashes/utils.js';
import { decode, encode } from 'cbor-x';

import type { Transport } from '../../transport/types';
import { DeviceClient } from '../client';
import { Reassembler, toFrames } from '../framing';
import { CipherState, pairCode, publicKeyOf } from '../noise';
import { handshake } from '../secure';

const nonce = (n: bigint) => {
  const iv = new Uint8Array(12);
  new DataView(iv.buffer).setBigUint64(4, n, true);
  return iv;
};
const hkdf2 = (ck: Uint8Array, ikm: Uint8Array) => {
  const prk = hmac(sha256, ck, ikm);
  const o1 = hmac(sha256, prk, Uint8Array.of(1));
  return [o1, hmac(sha256, prk, concatBytes(o1, Uint8Array.of(2)))] as const;
};

/** Minimal Noise XX responder + request handler (test double for the device). */
class FakeDevice implements Transport {
  readonly kind = 'ble' as const;
  readonly frameSize = 64; // small: forces multi-frame messages
  readonly name = 'fake tredder';
  readonly s = x25519.utils.randomSecretKey();
  private e = x25519.utils.randomSecretKey();
  private h: Uint8Array = utf8ToBytes('Noise_XX_25519_ChaChaPoly_SHA256');
  private ck: Uint8Array = this.h;
  private k: Uint8Array | null = null;
  private n = 0n;
  private re: Uint8Array = new Uint8Array(0);
  private step = 0;
  private tx?: CipherState;
  private rx?: CipherState;
  hash: Uint8Array = new Uint8Array(0);
  appStatic: Uint8Array = new Uint8Array(0);
  tamperNext = false;
  private frameCbs = new Set<(f: Uint8Array) => void>();
  private inbox = new Reassembler();

  constructor() {
    this.mixHash(utf8ToBytes('tredder/1'));
  }
  private mixHash(d: Uint8Array) {
    this.h = sha256(concatBytes(this.h, d));
  }
  private mixKey(ikm: Uint8Array) {
    [this.ck, this.k] = hkdf2(this.ck, ikm);
    this.n = 0n;
  }
  private enc(p: Uint8Array) {
    const c = this.k ? chacha20poly1305(this.k, nonce(this.n++), this.h).encrypt(p) : p;
    this.mixHash(c);
    return c;
  }
  private dec(c: Uint8Array) {
    const p = this.k ? chacha20poly1305(this.k, nonce(this.n++), this.h).decrypt(c) : c;
    this.mixHash(c);
    return p;
  }
  private out(m: Uint8Array) {
    for (const f of toFrames(m, this.frameSize)) queueMicrotask(() => this.frameCbs.forEach((cb) => cb(f)));
  }
  async send(frame: Uint8Array) {
    const m = this.inbox.push(frame);
    if (!m) return;
    if (this.step === 0) {
      this.re = m.slice(0, 32);
      this.mixHash(this.re);
      this.dec(new Uint8Array(0)); // msg1's empty payload is hashed too
      const ePub = publicKeyOf(this.e);
      this.mixHash(ePub);
      this.mixKey(x25519.getSharedSecret(this.e, this.re));
      const sc = this.enc(publicKeyOf(this.s));
      this.mixKey(x25519.getSharedSecret(this.s, this.re));
      this.out(concatBytes(ePub, sc, this.enc(new Uint8Array(0))));
      this.step = 1;
    } else if (this.step === 1) {
      this.appStatic = this.dec(m.slice(0, 48));
      this.mixKey(x25519.getSharedSecret(this.e, this.appStatic));
      this.dec(m.slice(48));
      const [k1, k2] = hkdf2(this.ck, new Uint8Array(0));
      this.rx = new CipherState(k1);
      this.tx = new CipherState(k2);
      this.hash = this.h;
      this.step = 2;
    } else {
      const req = decode(this.rx!.open(m)) as { id: number; type: string };
      const reply = encode({ id: req.id, ok: true, result: { model: 'tredder', firmware: 'fake', protocol: 1, initialized: true, locked: true, hasSweepSource: false } });
      const sealed = this.tx!.seal(reply);
      if (this.tamperNext) sealed[sealed.length - 1] = sealed[sealed.length - 1]! ^ 1;
      this.out(sealed);
    }
  }
  onFrame(cb: (f: Uint8Array) => void) {
    this.frameCbs.add(cb);
    return () => this.frameCbs.delete(cb);
  }
  onDisconnect() {
    return () => {};
  }
  closed = false;
  async close() {
    this.closed = true;
  }
}

describe('secure BLE path (handshake + sealed requests)', () => {
  it('handshakes, agrees on the pairing code and both static keys, and runs a request', async () => {
    const dev = new FakeDevice();
    const appKey = x25519.utils.randomSecretKey();
    const session = await handshake(dev, appKey);
    expect(session.remoteStatic).toEqual(publicKeyOf(dev.s));
    expect(dev.appStatic).toEqual(publicKeyOf(appKey));
    expect(pairCode(session.hash)).toBe(pairCode(dev.hash));
    const c = new DeviceClient(dev, undefined, session);
    const info = await c.request({ type: 'get-info' });
    expect(info.model).toBe('tredder');
  });

  it('a reply that fails authentication kills the session', async () => {
    const dev = new FakeDevice();
    const session = await handshake(dev, x25519.utils.randomSecretKey());
    const c = new DeviceClient(dev, undefined, session);
    dev.tamperNext = true;
    await expect(c.request({ type: 'get-info' })).rejects.toThrow(/Secure channel broken/);
    expect(dev.closed).toBe(true);
  });
});
