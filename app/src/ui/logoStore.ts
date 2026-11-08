/*
 * Token logos downloaded while the app runs, with decoys (logoPlan.ts), stored on the phone.
 *
 *   token list:  Uniswap default list (EVM chains) + Jupiter verified (Solana, top by market cap).
 *                The same file for every user: fetching it reveals nothing. Cached for a week.
 *   logos:       downloaded only through ensureLogos(): needed + decoys, shuffled, 2 at a time with
 *                random 150-900 ms gaps. Every file is kept (Paths.document/logos), decoys too, so
 *                no logo is requested twice.
 *   safety:      keyed by contract address from those curated lists, never by name. The
 *                hand-checked bundled logos (coinLogos.ts) win; testnet tokens are not on the lists,
 *                so nothing is requested for them (they keep their generated pattern).
 */
import { Directory, File, Paths } from 'expo-file-system';
import { useSyncExternalStore } from 'react';

import { getJson } from '../util/http';
import { planDownloads } from './logoPlan';

const UNISWAP = 'https://tokens.uniswap.org';
const JUPITER = 'https://lite-api.jup.ag/tokens/v2/tag?query=verified';
const SOL_TOP = 500;
const LIST_MAX_AGE_MS = 7 * 24 * 3600 * 1000;
const MAX_BYTES = 400_000;
const EVM: Record<number, string> = { 1: 'eth', 11155111: 'eth', 8453: 'base', 56: 'bnb', 137: 'polygon', 42161: 'arbitrum' };

let dir: Directory | null = null;
let index: Record<string, string> = {}; // key -> file name
let loaded = false;
let busy = false;
const subs = new Set<() => void>();
let version = 0;

function storage(): Directory {
  if (!dir) {
    dir = new Directory(Paths.document, 'logos');
    if (!dir.exists) dir.create({ idempotent: true, intermediates: true });
    const idx = new File(dir, 'index.json');
    try {
      if (idx.exists) index = JSON.parse(idx.textSync()) as Record<string, string>;
    } catch {
      index = {};
    }
    loaded = true;
  }
  return dir;
}

function notify() {
  version++;
  subs.forEach((f) => f());
}

/** file:// URI of a downloaded logo, or undefined. */
export function downloadedLogo(key: string): string | undefined {
  if (!loaded) {
    try {
      storage();
    } catch {
      return undefined;
    }
  }
  const name = index[key];
  return name && dir ? new File(dir, name).uri : undefined;
}

export function useDownloadedLogo(key: string | undefined): string | undefined {
  useSyncExternalStore(
    (f) => (subs.add(f), () => void subs.delete(f)),
    () => version,
  );
  return key ? downloadedLogo(key) : undefined;
}

const safeUrl = (u?: string) => {
  if (!u) return undefined;
  if (u.startsWith('ipfs://')) u = 'https://ipfs.io/ipfs/' + u.slice(7).replace(/^ipfs\//, '');
  if (!/^https:\/\//.test(u) || /\.svg(\?|$)/i.test(u)) return undefined; // React Native cannot draw SVG files
  return u;
};

async function tokenList(): Promise<Map<string, string>> {
  const cache = new File(Paths.cache, 'tokenlist.json');
  try {
    if (cache.exists) {
      const c = JSON.parse(cache.textSync()) as { at: number; entries: [string, string][] };
      if (Date.now() - c.at < LIST_MAX_AGE_MS) return new Map(c.entries);
    }
  } catch {
    // re-fetch below
  }
  const map = new Map<string, string>();
  const [uni, jup] = await Promise.allSettled([
    getJson<{ tokens: { chainId: number; address: string; logoURI?: string }[] }>(UNISWAP, 30_000),
    getJson<{ id: string; icon?: string; mcap?: number; isVerified?: boolean }[]>(JUPITER, 30_000),
  ]);
  if (uni.status === 'fulfilled')
    for (const t of uni.value.tokens) {
      const chain = EVM[t.chainId], url = safeUrl(t.logoURI);
      if (chain && url && /^0x[0-9a-fA-F]{40}$/.test(t.address)) map.set(`${chain}:${t.address.toLowerCase()}`, url);
    }
  if (jup.status === 'fulfilled')
    for (const t of jup.value.filter((x) => x.isVerified !== false).sort((a, b) => (b.mcap ?? 0) - (a.mcap ?? 0)).slice(0, SOL_TOP)) {
      const url = safeUrl(t.icon);
      if (url) map.set(`sol:${t.id}`, url);
    }
  if (map.size) cache.write(JSON.stringify({ at: Date.now(), entries: [...map] }));
  return map;
}

const sleep = (ms: number) => new Promise((ok) => setTimeout(ok, ms));

function fileName(key: string): string {
  let h = 2166136261; // FNV-1a: a short stable file name, nothing secret
  for (let i = 0; i < key.length; i++) h = Math.imul(h ^ key.charCodeAt(i), 16777619) >>> 0;
  return `${h.toString(16)}-${key.length}`;
}

/** Download the logos for these keys ("chain:address"), hidden among decoys. Safe to call often. */
export async function ensureLogos(keys: string[]): Promise<void> {
  if (busy) return;
  busy = true;
  try {
    const d = storage();
    const have = new Set(Object.keys(index));
    if (!keys.some((k) => !have.has(k))) return;
    const list = await tokenList();
    const batch = planDownloads(keys, list, have);
    if (!batch.length) return;
    const queue = batch.slice();
    await Promise.all(
      [0, 1].map(async () => {
        for (let k = queue.shift(); k; k = queue.shift()) {
          await sleep(150 + Math.random() * 750);
          try {
            const r = await fetch(list.get(k)!, { headers: { accept: 'image/*' } });
            if (!r.ok) continue;
            const bytes = new Uint8Array(await r.arrayBuffer());
            if (!bytes.length || bytes.length > MAX_BYTES) continue;
            const name = fileName(k);
            await new File(d, name).write(bytes);
            index[k] = name;
            notify();
          } catch {
            // one failed logo: that token keeps its pattern
          }
        }
      }),
    );
    await new File(d, 'index.json').write(JSON.stringify(index));
  } catch {
    // no storage or no network: patterns stay
  } finally {
    busy = false;
  }
}

