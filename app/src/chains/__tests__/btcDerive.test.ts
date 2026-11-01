// Cross-component check: the app's watch-only derivation must produce exactly
// the addresses the FIRMWARE CORE computed for the same account keys
// (src/dev/mockDeviceData.json is exported by firmware/tools/mock_export.c).
import { hexToBytes } from '@noble/hashes/utils.js';

import { MOCK as data } from '../../dev/mockData';
import type { Net, Scheme } from '../../protocol/types';
import { btcAddress, type XpubKey } from '../btcDerive';

describe('BTC watch-only derivation vs firmware', () => {
  const rows = data.btcAddresses;

  it('covers all 4 schemes, both networks, receive + change', () => {
    expect(new Set(rows.map((r) => r.scheme)).size).toBe(4);
    expect(new Set(rows.map((r) => r.net))).toEqual(new Set(['main', 'test']));
    expect(rows.some((r) => r.change === 1)).toBe(true);
  });

  it.each(rows.map((r) => [r.scheme, r.net, r.account, r.change, r.index, r.address] as const))(
    '%s %s acct %d %d/%d',
    (scheme, net, account, change, index, address) => {
      const k = data.accounts.find((a) => a.scheme === scheme && a.net === net && a.account === account);
      if (!k || k.kind !== 'xpub') throw new Error('missing account key');
      const key: XpubKey = {
        account,
        path: k.path,
        kind: 'xpub',
        publicKey: hexToBytes(k.publicKey),
        chainCode: hexToBytes(k.chainCode),
        masterFingerprint: k.masterFingerprint,
      };
      expect(btcAddress(key, scheme as Scheme, net as Net, change, index)).toBe(address);
    },
  );

  it('matches the official BIP84 vector', () => {
    const r = rows.find((x) => x.scheme === 'btc-p2wpkh' && x.net === 'main' && x.account === 0 && x.change === 0 && x.index === 0);
    expect(r?.address).toBe('bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu');
  });
});
