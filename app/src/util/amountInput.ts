/* Amount entry for the send keypad. Values are kept as the decimal string the user typed. */
import { formatAmount } from './amount';

export type Key = '0' | '1' | '2' | '3' | '4' | '5' | '6' | '7' | '8' | '9' | '.' | 'del';

/** Apply one keypad press. `decimals` caps the digits after the point (coin decimals, or 2 for USD). */
export function pressKey(cur: string, key: Key, decimals: number): string {
  if (key === 'del') return cur.slice(0, -1);
  if (key === '.') {
    if (decimals === 0 || cur.includes('.')) return cur;
    return (cur || '0') + '.';
  }
  const dot = cur.indexOf('.');
  if (dot >= 0 && cur.length - dot - 1 >= decimals) return cur; // no more decimals allowed
  if (cur === '0') return key; // "0" then "5" -> "5", not "05"
  if (cur.replace('.', '').length >= 24) return cur;
  return cur + key;
}

/** Whole-number-and-fraction string -> number (display maths only, never for the amount sent). */
export const asNumber = (s: string) => (s && s !== '.' ? Number(s) : 0);

/** USD typed by the user -> coin amount string with at most `decimals` (rounded down). */
export function usdToCoin(usd: string, price: number, decimals: number): string {
  const v = asNumber(usd);
  if (!(v > 0) || !(price > 0)) return '';
  const coin = v / price;
  const d = Math.min(decimals, 8);
  const f = Math.floor(coin * 10 ** d) / 10 ** d;
  return f > 0 ? f.toFixed(d).replace(/\.?0+$/, '') : '';
}

/** Coin amount string -> USD string with 2 decimals. */
export function coinToUsd(coin: string, price: number | undefined): string {
  const v = asNumber(coin);
  if (!(v > 0) || !(price && price > 0)) return '';
  return (v * price).toFixed(2);
}

/** Base units -> plain number for display maths (formatAmount adds thousands separators). */
export const unitsToNumber = (v: bigint, decimals: number) => Number(formatAmount(v, decimals, 12).replace(/,/g, ''));
/** formatAmount without separators: safe to put back into the amount being typed. */
export const unitsToInput = (v: bigint, decimals: number) => formatAmount(v, decimals, decimals).replace(/,/g, '');
