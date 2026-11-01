// Watch-only Bitcoin address derivation from a device-exported account key
// (public key + chain code). Public data only. Checked in tests against
// addresses computed by the firmware core for the same account keys.
import * as ecc from '@bitcoinerlab/secp256k1';
import { HDKey } from '@scure/bip32';
import { initEccLib, networks, payments, toXOnly, type Network } from 'bitcoinjs-lib';

import type { AccountKey, Net, Scheme } from '../protocol/types';

initEccLib(ecc);

export const btcNetwork = (net: Net): Network => (net === 'main' ? networks.bitcoin : networks.testnet);

export type XpubKey = Extract<AccountKey, { kind: 'xpub' }>;

const nodeCache = new WeakMap<XpubKey, HDKey>();

function accountNode(k: XpubKey): HDKey {
  let n = nodeCache.get(k);
  if (!n) {
    n = new HDKey({ publicKey: k.publicKey, chainCode: k.chainCode, depth: 3 });
    nodeCache.set(k, n);
  }
  return n;
}

export interface DerivedKey {
  pubkey: Uint8Array;
  change: number;
  index: number;
}

export function derivePubkey(k: XpubKey, change: number, index: number): DerivedKey {
  if (change !== 0 && change !== 1) throw new Error('change must be 0 or 1');
  const child = accountNode(k).deriveChild(change).deriveChild(index);
  if (!child.publicKey) throw new Error('derivation failed');
  return { pubkey: child.publicKey, change, index };
}

export interface BtcScript {
  address: string;
  output: Uint8Array;
  redeemScript?: Uint8Array; // P2SH-P2WPKH
  internalKey?: Uint8Array; // P2TR (x-only)
}

export function scriptFor(scheme: Scheme, net: Net, pubkey: Uint8Array): BtcScript {
  const network = btcNetwork(net);
  switch (scheme) {
    case 'btc-p2pkh': {
      const p = payments.p2pkh({ pubkey, network });
      return { address: p.address!, output: p.output! };
    }
    case 'btc-p2sh-p2wpkh': {
      const inner = payments.p2wpkh({ pubkey, network });
      const p = payments.p2sh({ redeem: inner, network });
      return { address: p.address!, output: p.output!, redeemScript: inner.output! };
    }
    case 'btc-p2wpkh': {
      const p = payments.p2wpkh({ pubkey, network });
      return { address: p.address!, output: p.output! };
    }
    case 'btc-p2tr': {
      const internalKey = toXOnly(pubkey);
      const p = payments.p2tr({ internalPubkey: internalKey, network }); // BIP86: key path only
      return { address: p.address!, output: p.output!, internalKey };
    }
    default:
      throw new Error(`${scheme} is not a Bitcoin scheme`);
  }
}

export function btcAddress(k: XpubKey, scheme: Scheme, net: Net, change: number, index: number): string {
  return scriptFor(scheme, net, derivePubkey(k, change, index).pubkey).address;
}

/** BIP32 path of a derived key, for PSBT bip32Derivation entries. */
export function fullPath(k: XpubKey, change: number, index: number) {
  return `${k.path}/${change}/${index}`;
}
