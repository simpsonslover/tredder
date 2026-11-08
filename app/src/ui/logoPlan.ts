/*
 * Which logos to download, hiding the ones this wallet needs among random decoys (pure, tested).
 *
 * Threat: every logo host (CoinGecko, IPFS gateways, project sites) sees the phone's IP and which
 * logo it asks for. Downloading only the tokens we hold would hand them a list of holdings.
 *
 *   - every needed logo comes with MIN_DECOYS..MAX_DECOYS random others from the same token list;
 *     a third of them, where possible, from the same host, so a rare host does not single out the
 *     real request
 *   - the batch is shuffled (and fetched slowly with random gaps, see logoStore.ts)
 *   - everything downloaded, decoys included, is kept forever: no logo is ever requested twice, so
 *     repeated app starts cannot be intersected to find the real ones
 *
 * Limits (honest): an observer of one batch knows the wallet holds at least one of its tokens,
 * each one with probability about 1 in (decoys + 1); a batch is only triggered by something new.
 */
export const MIN_DECOYS = 10;
export const MAX_DECOYS = 30;

export type Rng = () => number; // [0, 1)

function pick<T>(arr: T[], n: number, rnd: Rng): T[] {
  const a = arr.slice();
  const out: T[] = [];
  for (let i = 0; i < n && a.length; i++) out.push(a.splice(Math.floor(rnd() * a.length), 1)[0]!);
  return out;
}

export function shuffle<T>(arr: T[], rnd: Rng): T[] {
  const a = arr.slice();
  for (let i = a.length - 1; i > 0; i--) {
    const j = Math.floor(rnd() * (i + 1));
    [a[i], a[j]] = [a[j]!, a[i]!];
  }
  return a;
}

/**
 * @param needed  keys ("chain:address") of logos the wallet wants
 * @param list    every key of the token list -> its logo URL
 * @param have    keys already stored on the phone
 * @returns       keys to download, needed + decoys, shuffled; [] if nothing needed is on the list
 */
export function planDownloads(needed: string[], list: Map<string, string>, have: Set<string>, rnd: Rng = Math.random): string[] {
  const want = [...new Set(needed)].filter((k) => list.has(k) && !have.has(k));
  if (!want.length) return [];
  const taken = new Set(want);
  const free = () => [...list.keys()].filter((k) => !have.has(k) && !taken.has(k));
  const decoys: string[] = [];
  for (const k of want) {
    const n = MIN_DECOYS + Math.floor(rnd() * (MAX_DECOYS - MIN_DECOYS + 1));
    const host = hostOf(list.get(k)!);
    const sameHost = pick(free().filter((d) => hostOf(list.get(d)!) === host), Math.floor(n / 3), rnd);
    sameHost.forEach((d) => taken.add(d));
    const rest = pick(free(), n - sameHost.length, rnd);
    rest.forEach((d) => taken.add(d));
    decoys.push(...sameHost, ...rest);
  }
  return shuffle([...want, ...decoys], rnd);
}

export function hostOf(url: string): string {
  const m = /^https:\/\/([^/]+)/.exec(url);
  return m ? m[1]!.toLowerCase() : '';
}
