export class HttpError extends Error {
  constructor(
    public readonly status: number,
    message: string,
  ) {
    super(message);
    this.name = 'HttpError';
  }
}

async function request(url: string, init: RequestInit = {}, timeoutMs = 15_000): Promise<Response> {
  const ctl = new AbortController();
  const t = setTimeout(() => ctl.abort(), timeoutMs);
  try {
    const r = await fetch(url, { ...init, signal: ctl.signal });
    if (!r.ok) throw new HttpError(r.status, `${r.status} ${(await r.text().catch(() => '')).slice(0, 200)}`);
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
