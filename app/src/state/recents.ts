/*
 * Addresses this wallet has sent to, newest first, per network family (an EVM address is
 * the same on every EVM chain). Public data, but it reveals who you pay, so it lives in
 * the secure store like the rest of the wallet data, never in plain storage.
 */
import type { Family } from '../chains/config';
import type { Net } from '../protocol/types';
import { loadJson, saveJson, secureKV, type KV } from './storage';

export interface Recent {
  family: Family;
  net: Net;
  address: string;
  last: number; // unix ms
  count: number;
}

const KEY = 'recipients';
const MAX = 30;

export async function loadRecents(kv: KV = secureKV): Promise<Recent[]> {
  return (await loadJson<Recent[]>(kv, KEY)) ?? [];
}

export async function rememberRecipient(family: Family, net: Net, address: string, kv: KV = secureKV, now = Date.now()): Promise<Recent[]> {
  const all = await loadRecents(kv);
  const same = (r: Recent) => r.family === family && r.net === net && sameAddress(family, r.address, address);
  const old = all.find(same);
  const next = [{ family, net, address, last: now, count: (old?.count ?? 0) + 1 }, ...all.filter((r) => !same(r))].slice(0, MAX);
  await saveJson(kv, KEY, next);
  return next;
}

export const sameAddress = (family: Family, a: string, b: string) => (family === 'evm' ? a.toLowerCase() === b.toLowerCase() : a === b);
