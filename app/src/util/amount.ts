// Exact decimal <-> base-unit conversion (no floating point for money).

export function parseAmount(input: string, decimals: number): bigint {
  const s = input.trim().replace(/,/g, '');
  if (!/^\d+(\.\d*)?$|^\.\d+$/.test(s)) throw new Error('Enter a number like 0.015');
  const [whole = '0', frac = ''] = s.split('.');
  if (frac.length > decimals) throw new Error(`At most ${decimals} decimal places`);
  return BigInt(whole || '0') * 10n ** BigInt(decimals) + BigInt((frac + '0'.repeat(decimals)).slice(0, decimals) || '0');
}

export function formatAmount(v: bigint, decimals: number, maxFrac = 8): string {
  const neg = v < 0n;
  const a = neg ? -v : v;
  const base = 10n ** BigInt(decimals);
  const whole = a / base;
  let frac = (a % base).toString().padStart(decimals, '0').slice(0, Math.min(decimals, maxFrac)).replace(/0+$/, '');
  const w = whole.toString().replace(/\B(?=(\d{3})+(?!\d))/g, ',');
  return `${neg ? '-' : ''}${w}${frac ? '.' + frac : ''}`;
}

export function formatUsd(v: number | undefined): string {
  if (v === undefined || !Number.isFinite(v)) return '—';
  if (v > 0 && v < 0.01) return '< $0.01';
  return v.toLocaleString('en-US', { style: 'currency', currency: 'USD' });
}
