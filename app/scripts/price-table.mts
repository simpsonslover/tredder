// Builds src/prices/priceTable.ts: which price source may price which token.
//   npx tsx scripts/price-table.mts
//
// A token (chain + contract address) gets a source only if BOTH hold:
//   1. its address is in a curated list under that symbol (Uniswap default list for the EVM chains,
//      Jupiter's verified list for Solana, market cap >= $20M), and
//   2. that source's price for the symbol is within 3% of DexScreener's price for that exact address
//      (most liquid pair, >= $50k liquidity).
// Exchanges list coins by ticker, and tickers collide (Coinbase "JUP" is not Jupiter; Kraken "LIT" is
// not the Ethereum LIT token): check 2 catches that. Rerun now and then: tickers get reused.
//
// Sources: Coinbase (one request: every rate), Kraken (one request: the fixed pair list), Pyth price
// accounts on Solana (only the feeds Pyth keeps fresh there; read through the app's Solana server).
import { Connection, PublicKey } from '@solana/web3.js';
import { writeFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const OUT = fileURLToPath(new URL('../src/prices/priceTable.ts', import.meta.url));
const PYTH_PROGRAM = new PublicKey('pythWSnswVUd12oZpeFP8e9CVaEqJg25g1Vtc2biRsT');
const CHAIN_IDS: Record<number, string> = { 1: 'eth', 8453: 'base', 56: 'bnb', 137: 'polygon', 42161: 'arbitrum' };
const DEX: Record<string, string> = { eth: 'ethereum', base: 'base', bnb: 'bsc', polygon: 'polygon', arbitrum: 'arbitrum', sol: 'solana' };
const SKIP = new Set(['WETH', 'WBNB', 'WPOL', 'WMATIC', 'SOL', 'WSOL']);
/** Each chain's own coin: priced by the app from this fixed list (not from the token lists). */
export const NATIVE_KRAKEN = ['XXBTZUSD', 'XETHZUSD', 'SOLUSD', 'BNBUSD', 'POLUSD'];

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
async function get<T>(url: string): Promise<T> {
  const r = await fetch(url);
  if (!r.ok) throw new Error(`${r.status} ${url.slice(0, 80)}`);
  return (await r.json()) as T;
}

type Cand = { chain: string; address: string; symbol: string };
const cands = new Map<string, Cand>();
const uni = await get<{ tokens: { chainId: number; address: string; symbol: string }[] }>('https://tokens.uniswap.org');
for (const t of uni.tokens) {
  const chain = CHAIN_IDS[t.chainId];
  if (chain && t.symbol === t.symbol.toUpperCase() && !SKIP.has(t.symbol)) cands.set(`${chain}:${t.address.toLowerCase()}`, { chain, address: t.address.toLowerCase(), symbol: t.symbol });
}
const jup = await get<{ id: string; symbol: string; mcap?: number }[]>('https://lite-api.jup.ag/tokens/v2/tag?query=verified');
for (const t of jup) if ((t.mcap ?? 0) >= 20e6 && !SKIP.has(t.symbol.toUpperCase())) cands.set(`sol:${t.id}`, { chain: 'sol', address: t.id, symbol: t.symbol.toUpperCase() });
console.error('candidates', cands.size);

// Coinbase
const rates = (await get<{ data: { rates: Record<string, string> } }>('https://api.coinbase.com/v2/exchange-rates?currency=USD')).data.rates;
const cb: Record<string, number> = {};
for (const [k, v] of Object.entries(rates)) if (Number(v) > 0) cb[k] = 1 / Number(v);

// Kraken
const pairs = (await get<{ result: Record<string, { wsname: string; status?: string }> }>('https://api.kraken.com/0/public/AssetPairs')).result;
const krPair: Record<string, string> = {};
for (const [k, v] of Object.entries(pairs)) if (v.wsname?.endsWith('/USD') && (v.status ?? 'online') === 'online') krPair[v.wsname.split('/')[0]!] = k;
const kr: Record<string, number> = {};
const krWant = [...new Set([...cands.values()].map((c) => c.symbol))].filter((s) => krPair[s]);
for (let i = 0; i < krWant.length; i += 40) {
  const chunk = krWant.slice(i, i + 40);
  const r = await get<{ error: string[]; result: Record<string, { c: [string] }> }>('https://api.kraken.com/0/public/Ticker?pair=' + chunk.map((s) => krPair[s]).join(','));
  if (r.error.length) throw new Error('kraken: ' + r.error.join());
  for (const s of chunk) if (r.result[krPair[s]!]) kr[s] = Number(r.result[krPair[s]!]!.c[0]);
  await sleep(1200);
}

// Pyth on Solana: the feeds kept fresh there
const catalog = await get<{ id: string; attributes: { symbol: string } }[]>('https://hermes.pyth.network/v2/price_feeds?asset_type=crypto');
const feeds = catalog.filter((f) => f.attributes.symbol.startsWith('Crypto.') && f.attributes.symbol.endsWith('/USD'));
const conn = new Connection('https://api.mainnet-beta.solana.com');
const pythId: Record<string, string> = {};
const py: Record<string, number> = {};
for (let i = 0; i < feeds.length; i += 100) {
  const chunk = feeds.slice(i, i + 100);
  const accs = await conn.getMultipleAccountsInfo(chunk.map((f) => PublicKey.findProgramAddressSync([Buffer.from([0, 0]), Buffer.from(f.id, 'hex')], PYTH_PROGRAM)[0]));
  accs.forEach((a, j) => {
    if (!a) return;
    const d = a.data as Buffer;
    let o = 40;
    o += d[o] === 0 ? 2 : 1;
    if (d.subarray(o, o + 32).toString('hex') !== chunk[j]!.id) return;
    const price = Number(d.readBigInt64LE(o + 32)) * 10 ** d.readInt32LE(o + 48);
    if (Date.now() / 1000 - Number(d.readBigInt64LE(o + 52)) > 600) return; // not kept fresh
    const sym = chunk[j]!.attributes.symbol.slice(7, -4);
    pythId[sym] = chunk[j]!.id;
    py[sym] = price;
  });
}
console.error('pyth fresh on solana', Object.keys(py).length);

// DexScreener reference price per address
const ref = new Map<string, { usd: number; liq: number }>();
const byChain = new Map<string, string[]>();
for (const c of cands.values()) if (cb[c.symbol] || kr[c.symbol] || py[c.symbol]) byChain.set(c.chain, [...(byChain.get(c.chain) ?? []), c.address]);
for (const [chain, addrs] of byChain) {
  for (let i = 0; i < addrs.length; i += 30) {
    const ps = await get<{ baseToken: { address: string }; priceUsd?: string; liquidity?: { usd?: number } }[]>(`https://api.dexscreener.com/tokens/v1/${DEX[chain]}/${addrs.slice(i, i + 30).join(',')}`);
    for (const p of ps) {
      const a = chain === 'sol' ? p.baseToken.address : p.baseToken.address.toLowerCase();
      const liq = p.liquidity?.usd ?? 0;
      const k = `${chain}:${a}`;
      if (p.priceUsd && liq >= 50_000 && liq > (ref.get(k)?.liq ?? 0)) ref.set(k, { usd: Number(p.priceUsd), liq });
    }
    await sleep(400);
  }
}

type Route = { s: string; cb?: string; kr?: string; py?: string };
const table: Record<string, Route> = {};
const usedPyth = new Set<string>();
let rejected = 0;
for (const [k, c] of [...cands.entries()].sort(([a], [b]) => a.localeCompare(b))) {
  const r = ref.get(k);
  if (!r) continue;
  const ok = (p?: number) => p !== undefined && Math.abs(p - r.usd) / r.usd <= 0.03;
  const route: Route = { s: c.symbol };
  if (ok(cb[c.symbol])) route.cb = c.symbol;
  if (ok(kr[c.symbol])) route.kr = krPair[c.symbol];
  if (ok(py[c.symbol])) (route.py = c.symbol), usedPyth.add(c.symbol);
  if (route.cb || route.kr || route.py) table[k] = route;
  else if (cb[c.symbol] || kr[c.symbol] || py[c.symbol]) (rejected++, console.error('REJECT', k, c.symbol, 'dex', r.usd, 'cb', cb[c.symbol], 'kr', kr[c.symbol], 'py', py[c.symbol]));
}
usedPyth.add('SOL');

// The app asks Kraken for every pair in one request: check that works and the answer keys match.
const allKr = [...new Set([...NATIVE_KRAKEN, ...Object.values(table).flatMap((r) => (r.kr ? [r.kr] : []))])].sort();
const one = await get<{ error: string[]; result: Record<string, unknown> }>('https://api.kraken.com/0/public/Ticker?pair=' + allKr.join(','));
const missing = allKr.filter((p) => !one.result?.[p]);
if (one.error.length || missing.length) throw new Error(`kraken single request: ${one.error.join()} missing ${missing.join()}`);

const date = new Date().toISOString().slice(0, 10);
const lines = [
  `// GENERATED by scripts/price-table.mts on ${date}. Do not edit by hand: rerun the script.`,
  '// Token (`chain:address`, EVM lower-case) -> where its USD price may be read. Every source listed for a',
  '// token passed: address in a curated list under this symbol, and the source price within 3% of',
  "// DexScreener's price for this exact address. cb = Coinbase currency, kr = Kraken pair, py = Pyth feed.",
  'export type PriceRoute = { s: string; cb?: string; kr?: string; py?: string };',
  '',
  'export const TOKEN_ROUTES: Record<string, PriceRoute> = {',
  ...Object.entries(table).map(([k, r]) => `  ${JSON.stringify(k)}: ${JSON.stringify(r).replace(/"(\w+)":/g, '$1: ')},`),
  '};',
  '',
  '/** Kraken pairs, asked for all at once (fixed list: reveals nothing about holdings). */',
  `export const KRAKEN_PAIRS: string[] = ${JSON.stringify(allKr)};`,
  '',
  '/** Pyth feeds that Pyth keeps fresh on Solana (symbol -> feed id), read as Solana accounts. */',
  'export const PYTH_SOLANA: Record<string, string> = {',
  ...[...usedPyth].sort().map((s) => `  ${JSON.stringify(s)}: '${pythId[s]}',`),
  '};',
  '',
];
writeFileSync(OUT, lines.join('\n'));
console.error(`wrote ${Object.keys(table).length} tokens, ${allKr.length} Kraken pairs, ${usedPyth.size} Pyth feeds; ${rejected} rejected -> ${OUT}`);
