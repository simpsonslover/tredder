// CBOR encoding of protocol messages + strict shape checks on everything that
// comes back from the device (never trust the wire).
import { decode, encode } from 'cbor-x';

import { ERROR_CODES, type ErrorCode, type WireEvent, type WireRequest, type WireResponse } from './types';

export function encodeMessage(msg: WireRequest | WireResponse | WireEvent): Uint8Array {
  const b = encode(msg);
  return new Uint8Array(b.buffer, b.byteOffset, b.byteLength);
}

export function decodeMessage(bytes: Uint8Array): unknown {
  return decode(bytes);
}

const isObj = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null && !Array.isArray(v);

export function parseIncoming(bytes: Uint8Array): WireResponse | WireEvent {
  const v = decodeMessage(bytes);
  if (!isObj(v)) throw new Error('device message is not a map');
  if (v.type === 'event') {
    if (!['awaiting-user', 'locked', 'progress'].includes(String(v.event))) throw new Error('unknown event');
    return v as WireEvent;
  }
  if (typeof v.id !== 'number' || !Number.isInteger(v.id)) throw new Error('response without integer id');
  if (v.ok === true) return { id: v.id, ok: true, result: v.result };
  if (v.ok === false && isObj(v.error)) {
    const raw = String(v.error.code);
    const code: ErrorCode = (ERROR_CODES as readonly string[]).includes(raw) ? (raw as ErrorCode) : 'INTERNAL';
    return { id: v.id, ok: false, error: { code, message: String(v.error.message ?? '') } };
  }
  throw new Error('malformed response');
}

export function bytesOf(v: unknown, len?: number): Uint8Array {
  if (!(v instanceof Uint8Array)) throw new Error('expected bytes');
  if (len !== undefined && v.length !== len) throw new Error(`expected ${len} bytes, got ${v.length}`);
  return v;
}
