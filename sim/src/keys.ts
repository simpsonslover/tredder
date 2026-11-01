// Key derivation for every scheme, mirroring firmware/core/src/schemes.c and
// address.c. Verified in tests against data exported by the firmware core.
import { ed25519 } from '@noble/curves/ed25519.js';
import { hmac } from '@noble/hashes/hmac.js';
import { sha512 } from '@noble/hashes/sha2.js';
import { base58 } from '@scure/base';
import { HDKey } from '@scure/bip32';
import { mnemonicToSeedSync, validateMnemonic } from '@scure/bip39';
import { wordlist } from '@scure/bip39/wordlists/english.js';
import * as btc from '@scure/btc-signer';
import { privateKeyToAddress } from 'viem/accounts';
import { bytesToHex } from 'viem';

import type { AccountKey, Net, Scheme } from '../../app/src/protocol/types';

export const H = 0x80000000;
/** Hardened index. (Not `H | i`: JS bitwise ops are signed 32-bit and would go negative.) */
export const h = (i: number) => H + i;
const isHardened = (i: number) => i >= H;
export const WORDLIST = wordlist;

const BTC_PURPOSE: Partial<Record<Scheme, number>> = { 'btc-p2pkh': 44, 'btc-p2sh-p2wpkh': 49, 'btc-p2wpkh': 84, 'btc-p2tr': 86 };
export const isBtc = (s: Scheme) => s.startsWith('btc-');
export const isEvm = (s: Scheme) => s.startsWith('evm-');
export const isSol = (s: Scheme) => s.startsWith('sol-');

/** Same layout as firmware wl_scheme_path(). */
export function schemePath(s: Scheme, net: Net, account: number, change = 0, index = 0): number[] {
  const p = BTC_PURPOSE[s];
  if (p) return [h(p), h((net === 'test' ? 1 : 0)), h(account), change, index];
  if (change || index) throw new Error('change/index only apply to Bitcoin');
  switch (s) {
    case 'evm-bip44': return [h(44), h(60), h(0), 0, account];
    case 'evm-ledger-live': return [h(44), h(60), h(account), 0, 0];
    case 'sol-bip44-change': return [h(44), h(501), h(account), h(0)];
    case 'sol-bip44': return [h(44), h(501), h(account)];
    case 'sol-bip44-root': if (account) throw new Error('root path has only account 0'); return [h(44), h(501)];
    case 'sol-cli': if (account) throw new Error('CLI seed has only account 0'); return [];
    default: throw new Error(`unknown scheme ${s}`);
  }
}

export const pathString = (p: number[]) => 'm' + p.map((i) => `/${isHardened(i) ? i - H : i}${isHardened(i) ? "'" : ''}`).join('');

export function btcNetwork(net: Net) {
  return net === 'main' ? btc.NETWORK : btc.TEST_NETWORK;
}

export function btcPayment(s: Scheme, net: Net, pub: Uint8Array) {
  const n = btcNetwork(net);
  switch (s) {
    case 'btc-p2pkh': return btc.p2pkh(pub, n);
    case 'btc-p2sh-p2wpkh': return btc.p2sh(btc.p2wpkh(pub, n), n);
    case 'btc-p2wpkh': return btc.p2wpkh(pub, n);
    case 'btc-p2tr': return btc.p2tr(pub.subarray(1), undefined, n); // BIP86 key-path only
    default: throw new Error(`${s} is not Bitcoin`);
  }
}

/** SLIP-10 ed25519 (hardened only). */
function slip10(seed: Uint8Array, path: number[]) {
  let I = hmac(sha512, new TextEncoder().encode('ed25519 seed'), seed);
  let k = I.slice(0, 32);
  let c = I.slice(32);
  for (const i of path) {
    if (!isHardened(i)) throw new Error('ed25519 derivation is hardened-only');
    const data = new Uint8Array(37);
    data.set(k, 1);
    new DataView(data.buffer).setUint32(33, i >>> 0);
    I = hmac(sha512, c, data);
    k = I.slice(0, 32);
    c = I.slice(32);
  }
  return k;
}

export class WalletKeys {
  private readonly seed: Uint8Array;
  private readonly master: HDKey;

  constructor(mnemonic: string, passphrase = '') {
    if (!validateMnemonic(mnemonic, wordlist)) throw new Error('invalid mnemonic');
    this.seed = mnemonicToSeedSync(mnemonic, passphrase);
    this.master = HDKey.fromMasterSeed(this.seed);
  }

  get masterFingerprint() {
    return this.master.fingerprint;
  }

  private node(path: number[]) {
    let n = this.master;
    for (const i of path) n = n.deriveChild(i);
    return n;
  }

  /** secp256k1 private key at a BIP32 path (BTC, EVM). */
  secpKey(path: number[]): Uint8Array {
    const k = this.node(path).privateKey;
    if (!k) throw new Error('derivation failed');
    return k;
  }

  /** ed25519 private key for a Solana scheme. */
  solKey(s: Scheme, account: number): Uint8Array {
    return s === 'sol-cli' ? this.seed.slice(0, 32) : slip10(this.seed, schemePath(s, 'main', account));
  }

  solAddress(s: Scheme, account: number) {
    return base58.encode(ed25519.getPublicKey(this.solKey(s, account)));
  }

  evmAddress(s: Scheme, account: number) {
    return privateKeyToAddress(bytesToHex(this.secpKey(schemePath(s, 'main', account))) as `0x${string}`);
  }

  btcAddress(s: Scheme, net: Net, account: number, change: number, index: number) {
    const pub = this.node(schemePath(s, net, account, change, index)).publicKey!;
    return btcPayment(s, net, pub).address!;
  }

  address(s: Scheme, net: Net, account: number, change: number, index: number) {
    if (isBtc(s)) return this.btcAddress(s, net, account, change, index);
    if (change || index) throw new Error('change/index only apply to Bitcoin');
    return isEvm(s) ? this.evmAddress(s, account) : this.solAddress(s, account);
  }

  /** protocol get-accounts entry */
  accountKey(s: Scheme, net: Net, account: number): AccountKey {
    if (isBtc(s)) {
      const path = schemePath(s, net, account).slice(0, 3);
      const n = this.node(path);
      return { account, path: pathString(path), kind: 'xpub', publicKey: n.publicKey!, chainCode: n.chainCode!, masterFingerprint: this.masterFingerprint };
    }
    const path = s === 'sol-cli' ? 'seed[0:32]' : pathString(schemePath(s, 'main', account));
    return { account, path, kind: 'address', address: this.address(s, 'main', account, 0, 0) };
  }

  /** For PSBT signing: find the key whose (fingerprint, path) matches. */
  keyForPath(fingerprint: number, path: number[]): Uint8Array | null {
    if (fingerprint >>> 0 !== this.masterFingerprint >>> 0) return null;
    return this.secpKey(path);
  }
}
