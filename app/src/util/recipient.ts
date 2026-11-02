/*
 * Recipient handling for the send flow: read what the user pasted or scanned (plain
 * address or payment URI), and check it is a valid address for the chosen network.
 * Pure functions, no I/O.
 */
import { address as baddress } from 'bitcoinjs-lib';
import { getAddress, isAddress } from 'viem';

import { btcNetwork } from '../chains/btcDerive';
import { CHAINS, type ChainId } from '../chains/config';
import { isSolAddress } from '../chains/sol';
import type { Net } from '../protocol/types';

export interface ParsedInput {
  address: string;
  /** Amount from the URI, in whole coins as written (e.g. "0.01"); never applied silently. */
  amount?: string;
  /** Which family the URI scheme names, if any. */
  scheme?: 'btc' | 'evm' | 'sol';
}

/**
 * Accepts a bare address or a payment URI:
 *   bitcoin:<addr>?amount=0.01          (BIP-21)
 *   ethereum:<addr>[@chainId][?value=]  (EIP-681, plain transfers only; value is in wei)
 *   solana:<addr>?amount=0.5            (Solana Pay)
 */
export function parseRecipientInput(raw: string): ParsedInput {
  const s = raw.trim();
  const m = /^(bitcoin|ethereum|solana):(?:\/\/)?([^?@/]+)(?:@\d+)?(?:\/[^?]*)?(?:\?(.*))?$/i.exec(s);
  if (!m) return { address: s };
  const scheme = ({ bitcoin: 'btc', ethereum: 'evm', solana: 'sol' } as const)[m[1]!.toLowerCase() as 'bitcoin' | 'ethereum' | 'solana'];
  const out: ParsedInput = { address: m[2]!, scheme };
  const q = new URLSearchParams(m[3] ?? '');
  if (scheme === 'evm') {
    if (s.includes('/')) return { address: '', scheme }; // contract call URIs (token transfer etc.): not a plain address
    const v = q.get('value');
    if (v && /^\d+$/.test(v)) out.amount = weiToEth(v);
  } else {
    const a = q.get('amount');
    if (a && /^\d*\.?\d+$/.test(a)) out.amount = a;
  }
  return out;
}

function weiToEth(wei: string): string {
  const p = wei.padStart(19, '0');
  const whole = p.slice(0, -18).replace(/^0+(?=\d)/, '');
  const frac = p.slice(-18).replace(/0+$/, '');
  return frac ? `${whole}.${frac}` : whole;
}

export type Check = { ok: true; address: string; note?: string } | { ok: false; reason: string };

/** Is `addr` a valid recipient on `chain` for this network? Returns the normalised form. */
export function checkRecipient(chain: ChainId, net: Net, addr: string): Check {
  const a = addr.trim();
  if (!a) return { ok: false, reason: 'Enter an address' };
  const fam = CHAINS[chain].family;
  if (fam === 'btc') {
    try {
      baddress.toOutputScript(a, btcNetwork(net));
      return { ok: true, address: a };
    } catch {
      try {
        baddress.toOutputScript(a, btcNetwork(net === 'main' ? 'test' : 'main'));
        return { ok: false, reason: net === 'test' ? 'This is a mainnet Bitcoin address, but you are on testnet' : 'This is a testnet Bitcoin address' };
      } catch {
        const k = looksLike(a);
        return { ok: false, reason: k ? `This is a ${k} address, not a Bitcoin one` : 'Not a valid Bitcoin address' };
      }
    }
  }
  if (fam === 'evm') {
    if (!isAddress(a, { strict: false })) {
      const k = looksLike(a);
      return { ok: false, reason: k ? `This is a ${k} address, not an ${CHAINS[chain].net[net].label} one` : 'Not a valid address' };
    }
    // Mixed case means the sender included a checksum: it must be right (catches typos)
    if (a !== a.toLowerCase() && a.slice(2) !== a.slice(2).toUpperCase() && !isAddress(a, { strict: true }))
      return { ok: false, reason: 'Checksum does not match: the address has a typo' };
    return { ok: true, address: getAddress(a) };
  }
  if (!isSolAddress(a)) {
    const k = looksLike(a);
    return { ok: false, reason: k ? `This is a ${k} address, not a Solana one` : 'Not a valid Solana address' };
  }
  return { ok: true, address: a };
}

/** Which kind of address this is, if it is one at all (for "wrong network" messages). */
function looksLike(a: string): 'Bitcoin' | 'EVM' | 'Solana' | null {
  if (/^0x[0-9a-fA-F]{40}$/.test(a)) return 'EVM';
  for (const n of ['main', 'test'] as const) {
    try {
      baddress.toOutputScript(a, btcNetwork(n));
      return 'Bitcoin';
    } catch {
      /* not this network */
    }
  }
  return isSolAddress(a) ? 'Solana' : null;
}

/** "bc1qxy…7k2m": the start and end people actually compare. */
export function shortAddress(a: string, n = 4): string {
  const head = a.startsWith('0x') ? n + 2 : n;
  return a.length <= head + n + 1 ? a : `${a.slice(0, head)}…${a.slice(-n)}`;
}

/** Split for display: [first 6, middle, last 6], so the parts to check stand out. */
export function addressParts(a: string, n = 6): [string, string, string] {
  if (a.length <= 2 * n) return [a, '', ''];
  return [a.slice(0, n), a.slice(n, -n), a.slice(-n)];
}
