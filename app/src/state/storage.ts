// Persistent app data in the OS secure keystore (expo-secure-store). Only
// PUBLIC wallet data is stored (account public keys, addresses, settings, sweep
// progress), but it reveals your balances, so it stays out of plain storage
// and out of Android backups (app.json: allowBackup = false).
import * as SecureStore from 'expo-secure-store';

const CHUNK = 1800; // some platforms reject values > 2048 bytes

export interface KV {
  get(key: string): Promise<string | null>;
  set(key: string, value: string): Promise<void>;
  del(key: string): Promise<void>;
}

export const secureKV: KV = {
  get: (k) => SecureStore.getItemAsync(k),
  set: (k, v) => SecureStore.setItemAsync(k, v),
  del: (k) => SecureStore.deleteItemAsync(k),
};

const safe = (k: string) => k.replace(/[^A-Za-z0-9._-]/g, '_');

export async function saveJson(kv: KV, key: string, value: unknown) {
  const s = JSON.stringify(value, (_k, v) => (typeof v === 'bigint' ? { $big: v.toString() } : v instanceof Uint8Array ? { $hex: toHex(v) } : v));
  const old = Number((await kv.get(safe(`${key}.n`))) ?? 0);
  const n = Math.ceil(s.length / CHUNK) || 1;
  for (let i = 0; i < n; i++) await kv.set(safe(`${key}.${i}`), s.slice(i * CHUNK, (i + 1) * CHUNK));
  for (let i = n; i < old; i++) await kv.del(safe(`${key}.${i}`));
  await kv.set(safe(`${key}.n`), String(n));
}

export async function loadJson<T>(kv: KV, key: string): Promise<T | null> {
  const n = Number((await kv.get(safe(`${key}.n`))) ?? 0);
  if (!n) return null;
  let s = '';
  for (let i = 0; i < n; i++) s += (await kv.get(safe(`${key}.${i}`))) ?? '';
  try {
    return JSON.parse(s, (_k, v) =>
      v && typeof v === 'object' && '$big' in v ? BigInt(v.$big) : v && typeof v === 'object' && '$hex' in v ? fromHex(v.$hex) : v,
    ) as T;
  } catch {
    return null;
  }
}

export async function removeJson(kv: KV, key: string) {
  const n = Number((await kv.get(safe(`${key}.n`))) ?? 0);
  for (let i = 0; i < n; i++) await kv.del(safe(`${key}.${i}`));
  await kv.del(safe(`${key}.n`));
}

const toHex = (b: Uint8Array) => Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('');
const fromHex = (h: string) => Uint8Array.from(h.match(/.{2}/g) ?? [], (x) => parseInt(x, 16));
