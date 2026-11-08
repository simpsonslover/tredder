import { MAX_DECOYS, MIN_DECOYS, hostOf, planDownloads } from '../logoPlan';

// a deterministic RNG (mulberry32) so failures are reproducible
const rng = (seed: number) => () => {
  seed |= 0;
  seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};

// 600 listed tokens on three hosts; one rare host with only 5
const list = new Map<string, string>();
for (let i = 0; i < 600; i++) list.set(`eth:0x${i.toString(16).padStart(40, '0')}`, `https://${['assets.coingecko.com', 'coin-images.coingecko.com', 'raw.githubusercontent.com'][i % 3]}/logo/${i}.png`);
for (let i = 0; i < 5; i++) list.set(`sol:rare${i}`, `https://ipfs.io/ipfs/rare${i}`);
const keys = [...list.keys()];

describe('logo downloads hide the wallet among decoys', () => {
  it('each needed logo comes with 10-30 decoys, no duplicates', () => {
    for (let s = 1; s <= 50; s++) {
      const need = [keys[7]!, keys[300]!];
      const b = planDownloads(need, list, new Set(), rng(s));
      expect(new Set(b).size).toBe(b.length);
      for (const k of need) expect(b).toContain(k);
      const decoys = b.length - need.length;
      expect(decoys).toBeGreaterThanOrEqual(need.length * MIN_DECOYS);
      expect(decoys).toBeLessThanOrEqual(need.length * MAX_DECOYS);
      for (const k of b) expect(list.has(k)).toBe(true);
    }
  });

  it('a token on a rare host gets decoys from that host too', () => {
    const b = planDownloads(['sol:rare0'], list, new Set(), rng(3));
    expect(b.filter((k) => hostOf(list.get(k)!) === 'ipfs.io').length).toBeGreaterThan(1);
  });

  it('never re-downloads: stored logos are neither needed nor used as decoys', () => {
    const have = new Set(keys.slice(0, 400));
    expect(planDownloads([keys[5]!], list, have, rng(1))).toEqual([]); // already stored: no request at all
    const b = planDownloads([keys[450]!], list, have, rng(1));
    expect(b.length).toBeGreaterThan(MIN_DECOYS);
    for (const k of b) expect(have.has(k)).toBe(false);
  });

  it('tokens not on the list (testnet junk) cause no requests', () => {
    expect(planDownloads(['eth:0xdeadbeef', 'sol:nope'], list, new Set(), rng(1))).toEqual([]);
  });

  it('the real logo is not at a predictable position', () => {
    const pos = new Set<number>();
    for (let s = 1; s <= 40; s++) pos.add(planDownloads([keys[10]!], list, new Set(), rng(s)).indexOf(keys[10]!));
    expect(pos.size).toBeGreaterThan(15);
  });
});
