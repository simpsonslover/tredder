/*
 * Generated icon for a token without a verified logo: a symmetric 5x5 pattern + colours derived
 * from SHA-256 of "chain:contract". Computed on the phone (no icon is ever fetched from a server,
 * which would tell a third party what this wallet holds).
 *
 * Why not the "real" logo: anyone can deploy a token named USDT; a brand logo would make a scam
 * token look genuine. A pattern from the contract address is unique per contract, so two tokens
 * with the same name but different contracts look different, a clone stands out.
 */
import { sha256 } from '@noble/hashes/sha2.js';
import { utf8ToBytes } from '@noble/hashes/utils.js';
import Svg, { Circle, ClipPath, Defs, G, Rect } from 'react-native-svg';

export interface IdenticonPattern {
  cells: boolean[]; // 5x5, row-major, mirrored left/right
  fg: string;
  bg: string;
}

function hsl(h: number, s: number, l: number): string {
  const a = s * Math.min(l, 1 - l);
  const f = (n: number) => {
    const k = (n + h / 30) % 12;
    return Math.round((l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1))) * 255)
      .toString(16)
      .padStart(2, '0');
  };
  return `#${f(0)}${f(8)}${f(4)}`;
}

/** Deterministic pattern for a seed (exported for tests). */
export function identiconPattern(seed: string): IdenticonPattern {
  const h = sha256(utf8ToBytes(seed));
  const cells: boolean[] = [];
  for (let r = 0; r < 5; r++)
    for (let c = 0; c < 5; c++) {
      const col = c < 3 ? c : 4 - c; // mirror: columns 3,4 copy 1,0
      cells.push((h[r * 3 + col]! & 1) === 1);
    }
  if (!cells.some(Boolean)) cells[12] = true; // never an empty icon
  const hue = ((h[20]! << 8) | h[21]!) % 360;
  return { cells, fg: hsl(hue, 0.65, 0.62), bg: hsl((hue + 180) % 360, 0.25, 0.22) };
}

export function Identicon({ seed, size }: { seed: string; size: number }) {
  const p = identiconPattern(seed);
  const pad = size * 0.2, cell = (size - 2 * pad) / 5;
  const id = `clip-${size}`;
  return (
    <Svg width={size} height={size}>
      <Defs>
        <ClipPath id={id}>
          <Circle cx={size / 2} cy={size / 2} r={size / 2} />
        </ClipPath>
      </Defs>
      <G clipPath={`url(#${id})`}>
        <Rect x={0} y={0} width={size} height={size} fill={p.bg} />
        {p.cells.map((on, i) =>
          on ? <Rect key={i} x={pad + (i % 5) * cell} y={pad + Math.floor(i / 5) * cell} width={cell + 0.5} height={cell + 0.5} fill={p.fg} /> : null,
        )}
      </G>
    </Svg>
  );
}
