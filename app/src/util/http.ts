export class HttpError extends Error {
  constructor(
    public readonly status: number,
    message: string,
  ) {
    super(message);
    this.name = 'HttpError';
  }
}

const host = (url: string) => {
  try {
    return new URL(url).host;
  } catch {
    return url;
  }
};

/** One short line for the screen, whatever failed (fetch, viem, Solana web3). */
export function friendlyError(e: unknown): string {
  const m = String((e as Error)?.message ?? e).split('\n')[0]!;
  if (e instanceof HttpError) return m;
  if (/network request failed|fetch failed|failed to fetch|ssl|tls|certificate|ENOTFOUND|ECONN|socket|timed? ?out|aborted|HTTP request failed/i.test(m))
    return 'could not reach the service (offline, or blocked by a web filter on this network)';
  if (/429|rate.?limit|too many requests/i.test(m)) return 'the service is rate-limiting requests; try again in a minute';
  if (/\b403\b/.test(m)) return 'the service refused the request (403)';
  return m.length > 140 ? m.slice(0, 140) + '…' : m;
}

async function request(url: string, init: RequestInit = {}, timeoutMs = 15_000): Promise<Response> {
  const ctl = new AbortController();
  const t = setTimeout(() => ctl.abort(), timeoutMs);
  try {
    const r = await fetch(url, { ...init, signal: ctl.signal });
    if (!r.ok) {
      const body = await r.text().catch(() => '');
      // An HTML page instead of data: bot protection (e.g. Cloudflare "Just a moment...") or a web
      // filter. Never show the raw page.
      const html = /^\s*<(!doctype|html)/i.test(body);
      throw new HttpError(r.status, html ? `HTTP ${r.status}: ${host(url)} sent a web page instead of data (bot protection or a web filter)` : `HTTP ${r.status} from ${host(url)}: ${body.slice(0, 120)}`);
    }
    return r;
  } finally {
    clearTimeout(t);
  }
}

export const getJson = async <T>(url: string, timeoutMs?: number): Promise<T> =>
  (await request(url, { headers: { accept: 'application/json' } }, timeoutMs)).json() as Promise<T>;

export const getText = async (url: string): Promise<string> => (await request(url)).text();

export const postText = async (url: string, body: string): Promise<string> =>
  (await request(url, { method: 'POST', body, headers: { 'content-type': 'text/plain' } })).text();

/** Run async jobs with bounded concurrency (public APIs rate-limit). */
export async function pool<T, R>(items: T[], limit: number, fn: (item: T) => Promise<R>): Promise<R[]> {
  const out: R[] = new Array(items.length);
  let next = 0;
  const workers = Array.from({ length: Math.min(limit, items.length) }, async () => {
    while (next < items.length) {
      const i = next++;
      out[i] = await fn(items[i]!);
    }
  });
  await Promise.all(workers);
  return out;
}
