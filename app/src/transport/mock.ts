// In-app MOCK DEVICE for development and demos.
//
// - Serves only PUBLIC data for the public test phrase "abandon x11 about",
//   exported from the real firmware core (src/dev/mockDeviceData.json).
// - Holds no keys and cannot sign: every sign-* request is refused.
// - Speaks the real protocol: frames + CBOR in both directions, so the app's
//   client, framing and codec are exercised exactly as with hardware.
import { hexToBytes } from '@noble/hashes/utils.js';

import { btcAddress } from '../chains/btcDerive';
import { MOCK as data, type MockAccount } from '../dev/mockData';
import { decodeMessage, encodeMessage } from '../protocol/codec';
import { Reassembler, toFrames } from '../protocol/framing';
import type { AccountKey, Net, Scheme, WireEvent, WireRequest, WireResponse } from '../protocol/types';
import type { Transport } from './types';

export interface MockOptions {
  /** Simulated time the user takes to press Confirm (ms). */
  userDelayMs?: number;
  /** Make the simulated user press Back instead of Confirm. */
  rejectAll?: boolean;
  frameSize?: number;
}

export class MockTransport implements Transport {
  readonly kind = 'mock' as const;
  readonly secure = false;
  readonly name = 'Mock device (test phrase, cannot sign)';
  readonly frameSize: number;
  private frameCbs = new Set<(f: Uint8Array) => void>();
  private discCbs = new Set<() => void>();
  private rx = new Reassembler();
  private closed = false;

  constructor(private readonly opts: MockOptions = {}) {
    this.frameSize = opts.frameSize ?? 20; // smallest BLE MTU: stresses framing
  }

  async send(frame: Uint8Array): Promise<void> {
    if (this.closed) throw new Error('closed');
    const msg = this.rx.push(frame);
    if (msg) void this.handle(decodeMessage(msg) as WireRequest);
  }

  onFrame(cb: (f: Uint8Array) => void) {
    this.frameCbs.add(cb);
    return () => this.frameCbs.delete(cb);
  }

  onDisconnect(cb: () => void) {
    this.discCbs.add(cb);
    return () => this.discCbs.delete(cb);
  }

  async close() {
    this.closed = true;
    this.discCbs.forEach((cb) => cb());
  }

  private emit(msg: WireResponse | WireEvent) {
    for (const f of toFrames(encodeMessage(msg), this.frameSize)) this.frameCbs.forEach((cb) => cb(f));
  }

  private async userAction(id: number): Promise<boolean> {
    this.emit({ type: 'event', event: 'awaiting-user', id });
    await new Promise((r) => setTimeout(r, this.opts.userDelayMs ?? 900));
    return !this.opts.rejectAll;
  }

  private async handle(req: WireRequest) {
    const ok = (result: unknown) => this.emit({ id: req.id, ok: true, result });
    const err = (code: 'USER_REJECTED' | 'UNSUPPORTED' | 'INVALID_REQUEST', message: string) =>
      this.emit({ id: req.id, ok: false, error: { code, message } });

    switch (req.type) {
      case 'get-info':
        return ok({ model: 'mock', firmware: '0.0.0-mock', protocol: 1, initialized: true, locked: false, hasSweepSource: true });
      case 'get-accounts': {
        const net = isBtc(req.scheme) ? req.net : 'main';
        const accounts = req.accounts
          .map((a) => data.accounts.find((m) => m.net === net && m.scheme === req.scheme && m.account === a))
          .filter((m): m is MockAccount => !!m)
          .map(toAccountKey);
        return ok({ accounts });
      }
      case 'show-address': {
        const address = mockAddress(req.scheme, req.net, req.account, req.change, req.index);
        if (!address) return err('UNSUPPORTED', 'mock has no data for that address');
        const confirmed = await this.userAction(req.id);
        return confirmed ? ok({ address, confirmed: true }) : err('USER_REJECTED', 'Rejected on device');
      }
      case 'sweep-plan': {
        const confirmed = await this.userAction(req.id);
        return confirmed ? ok({ approved: true, planHash: `mock-${req.plan.id}` }) : err('USER_REJECTED', 'Rejected on device');
      }
      case 'forget-sweep-source': {
        const confirmed = await this.userAction(req.id);
        return confirmed ? ok({ erased: true }) : err('USER_REJECTED', 'Rejected on device');
      }
      case 'sign-btc':
      case 'sign-evm':
      case 'sign-sol':
        return err('UNSUPPORTED', 'The mock device holds no keys and cannot sign. Use real hardware.');
      case 'lock':
        return ok({});
      default:
        return err('INVALID_REQUEST', 'unknown request');
    }
  }
}

const isBtc = (s: string) => s.startsWith('btc-');

function toAccountKey(m: MockAccount): AccountKey {
  if (m.kind === 'xpub')
    return {
      account: m.account,
      path: m.path,
      kind: 'xpub',
      publicKey: hexToBytes(m.publicKey),
      chainCode: hexToBytes(m.chainCode),
      masterFingerprint: m.masterFingerprint,
    };
  return { account: m.account, path: m.path, kind: 'address', address: m.address };
}

function mockAddress(scheme: Scheme, net: Net, account: number, change: number, index: number): string | undefined {
  if (isBtc(scheme)) {
    const known = data.btcAddresses.find(
      (b) => b.scheme === scheme && b.net === net && b.account === account && b.change === change && b.index === index,
    )?.address;
    if (known) return known;
    // Any other index: derive from the exported PUBLIC account key (as a real device would show it).
    const k = data.accounts.find((a) => a.scheme === scheme && a.net === net && a.account === account);
    return k?.kind === 'xpub' ? btcAddress(toAccountKey(k) as Extract<AccountKey, { kind: 'xpub' }>, scheme, net, change, index) : undefined;
  }
  if (change || index) return undefined;
  const m = data.accounts.find((a) => a.scheme === scheme && a.account === account && a.kind === 'address');
  return m?.kind === 'address' ? m.address : undefined;
}
