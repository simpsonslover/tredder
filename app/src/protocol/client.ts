// Request/response client on top of a Transport: framing + CBOR + ids +
// timeouts + validation of every result before the app uses it.
import type { Transport } from '../transport/types';
import { bytesOf, encodeMessage, parseIncoming } from './codec';
import { Reassembler, toFrames } from './framing';
import type { Session } from './noise';
import {
  type AccountKey,
  DeviceError,
  NEEDS_USER,
  type Request,
  type ResultMap,
  type WireEvent,
  type WireResponse,
} from './types';

type Pending = { resolve: (v: unknown) => void; reject: (e: Error) => void; timer: ReturnType<typeof setTimeout>; type: Request['type'] };

export const TIMEOUT_MS = 15_000;

/** React Native defines __DEV__ (false in release builds). Node/tests: treated as dev. */
const isDevBuild = () => (globalThis as { __DEV__?: boolean }).__DEV__ ?? true;
export const USER_TIMEOUT_MS = 300_000;

export class DeviceClient {
  private nextId = 1;
  private pending = new Map<number, Pending>();
  private rx = new Reassembler();
  private unsub: (() => void)[] = [];

  /**
   * @param session Noise session from secure.ts handshake(). Required for real BLE: every
   *   message is then sealed / opened with it (protocol §3).
   */
  constructor(
    private readonly transport: Transport,
    private readonly onEvent: (e: WireEvent) => void = () => {},
    private readonly session?: Session,
  ) {
    if (transport.kind === 'sim' && !isDevBuild()) {
      // The desktop simulator speaks plaintext over localhost: development builds only.
      throw new DeviceError('UNSUPPORTED', 'The simulator can only be used in development builds.');
    }
    if (transport.kind === 'ble' && !session) {
      // Protocol §3: never talk plaintext to a real device.
      throw new DeviceError('UNSUPPORTED', 'No secure channel: refusing to talk to a real device in plaintext.');
    }
    this.unsub.push(transport.onFrame((f) => this.onFrame(f)));
    this.unsub.push(transport.onDisconnect(() => this.failAll(new DeviceError('INTERNAL', 'Device disconnected'))));
  }

  get transportName() {
    return this.transport.name;
  }

  async request<K extends Request['type']>(req: Extract<Request, { type: K }>): Promise<ResultMap[K]> {
    const id = this.nextId++;
    const timeout = NEEDS_USER.has(req.type) ? USER_TIMEOUT_MS : TIMEOUT_MS;
    const plain = encodeMessage({ ...req, id } as Request & { id: number });
    const bytes = this.session ? this.session.send.seal(plain) : plain;
    const result = new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new DeviceError('TIMEOUT', `${req.type} timed out`));
      }, timeout);
      this.pending.set(id, { resolve, reject, timer, type: req.type });
    });
    try {
      for (const f of toFrames(bytes, this.transport.frameSize)) await this.transport.send(f);
    } catch (e) {
      this.settle(id, new DeviceError('INTERNAL', `send failed: ${(e as Error).message}`));
    }
    return validate(req.type, await result) as ResultMap[K];
  }

  async close() {
    this.unsub.forEach((u) => u());
    this.failAll(new DeviceError('INTERNAL', 'closed'));
    await this.transport.close();
  }

  private onFrame(frame: Uint8Array) {
    let msg: Uint8Array | null;
    try {
      msg = this.rx.push(frame);
    } catch {
      return; // malformed framing: drop; pending requests will time out
    }
    if (!msg) return;
    if (this.session) {
      try {
        msg = this.session.recv.open(msg);
      } catch {
        // Forged, replayed or reordered: the device has dropped the session as well.
        this.failAll(new DeviceError('INTERNAL', 'Secure channel broken. Reconnect to the device.'));
        void this.transport.close();
        return;
      }
    }
    let parsed: WireResponse | WireEvent;
    try {
      parsed = parseIncoming(msg);
    } catch {
      return;
    }
    if ('type' in parsed) {
      this.onEvent(parsed);
      return;
    }
    if (parsed.ok) this.settle(parsed.id, null, parsed.result);
    else this.settle(parsed.id, new DeviceError(parsed.error.code, parsed.error.message));
  }

  private settle(id: number, err: Error | null, value?: unknown) {
    const p = this.pending.get(id);
    if (!p) return; // unknown / late id: ignore
    clearTimeout(p.timer);
    this.pending.delete(id);
    if (err) p.reject(err);
    else p.resolve(value);
  }

  private failAll(err: Error) {
    for (const id of [...this.pending.keys()]) this.settle(id, err);
  }
}

// ---------------------------------------------------------------- result validation

const fail = (t: string, why: string): never => {
  throw new DeviceError('INTERNAL', `invalid ${t} result: ${why}`);
};
const isObj = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null;

export function validate(type: Request['type'], r: unknown): unknown {
  if (!isObj(r)) return fail(type, 'not a map');
  switch (type) {
    case 'get-info':
      for (const k of ['model', 'firmware']) if (typeof r[k] !== 'string') fail(type, k);
      for (const k of ['initialized', 'locked', 'hasSweepSource']) if (typeof r[k] !== 'boolean') fail(type, k);
      if (typeof r.protocol !== 'number') fail(type, 'protocol');
      return r;
    case 'get-accounts': {
      if (!Array.isArray(r.accounts)) fail(type, 'accounts');
      const accounts = (r.accounts as unknown[]).map((a): AccountKey => {
        if (!isObj(a) || typeof a.account !== 'number' || typeof a.path !== 'string') return fail(type, 'entry');
        if (a.kind === 'xpub') {
          if (typeof a.masterFingerprint !== 'number') fail(type, 'fingerprint');
          return {
            account: a.account,
            path: a.path,
            kind: 'xpub',
            publicKey: bytesOf(a.publicKey, 33),
            chainCode: bytesOf(a.chainCode, 32),
            masterFingerprint: a.masterFingerprint as number,
          };
        }
        if (a.kind === 'address' && typeof a.address === 'string' && a.address.length < 100)
          return { account: a.account, path: a.path, kind: 'address', address: a.address };
        return fail(type, 'kind');
      });
      return { accounts };
    }
    case 'show-address':
      if (typeof r.address !== 'string' || typeof r.confirmed !== 'boolean') fail(type, 'fields');
      return r;
    case 'sign-btc':
      return { psbt: bytesOf(r.psbt) };
    case 'sign-evm':
      if (!/^0x[0-9a-fA-F]{1,64}$/.test(String(r.r)) || !/^0x[0-9a-fA-F]{1,64}$/.test(String(r.s))) fail(type, 'r/s');
      if (r.yParity !== 0 && r.yParity !== 1) fail(type, 'yParity');
      return r;
    case 'sign-sol':
      return { signature: bytesOf(r.signature, 64), publicKey: String(r.publicKey) };
    case 'sweep-plan':
      if (typeof r.approved !== 'boolean' || typeof r.planHash !== 'string') fail(type, 'fields');
      return r;
    case 'forget-sweep-source':
      if (typeof r.erased !== 'boolean') fail(type, 'erased');
      return r;
    case 'lock':
      return {};
  }
}
