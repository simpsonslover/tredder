// Keys for the Noise channel (protocol §3), kept in the OS secure keystore:
//  - this app's static X25519 key (generated once, never sent anywhere; the device stores
//    only its public half when the user accepts the pairing code);
//  - per device (BLE id), the device's static public key, pinned after the first successful
//    paired request. A different key later means a reset device or a different device: the
//    app then refuses until the user explicitly pairs again and compares the code.
import { bytesToHex, hexToBytes } from '@noble/hashes/utils.js';

import { newStaticKey } from '../protocol/noise';
import { secureKV, type KV } from './storage';

const STATIC = 'noise.static';
const PINS = 'noise.pins';

export async function appStaticKey(kv: KV = secureKV): Promise<Uint8Array> {
  const hex = await kv.get(STATIC);
  if (hex && /^[0-9a-f]{64}$/.test(hex)) return hexToBytes(hex);
  const k = newStaticKey();
  await kv.set(STATIC, bytesToHex(k));
  return k;
}

async function pins(kv: KV): Promise<Record<string, string>> {
  try {
    return JSON.parse((await kv.get(PINS)) ?? '{}') as Record<string, string>;
  } catch {
    return {};
  }
}

const pinKey = (deviceId: string) => deviceId.toLowerCase();

export async function pinnedDeviceKey(deviceId: string, kv: KV = secureKV): Promise<string | null> {
  return (await pins(kv))[pinKey(deviceId)] ?? null;
}

export async function pinDeviceKey(deviceId: string, key: Uint8Array, kv: KV = secureKV): Promise<void> {
  const p = await pins(kv);
  p[pinKey(deviceId)] = bytesToHex(key);
  await kv.set(PINS, JSON.stringify(p));
}

/** Thrown by connectBle when the device's identity key is not the pinned one. */
export class IdentityChangedError extends Error {
  constructor() {
    super(
      'This device has a different identity key than last time. That happens after the demo firmware restarts or a device is wiped, but it is also exactly what a fake device would look like.',
    );
    this.name = 'IdentityChangedError';
  }
}
