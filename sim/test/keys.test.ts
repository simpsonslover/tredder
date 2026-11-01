// The simulator must derive exactly what the FIRMWARE CORE derives
// (app/src/dev/mockDeviceData.json is exported by firmware/tools/mock_export.c).
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { bytesToHex } from '@noble/hashes/utils.js';

import { MOCK } from '../../app/src/dev/mockData';
import { WalletKeys } from '../src/keys';

const ABANDON = 'abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about';
const keys = new WalletKeys(ABANDON);

test('master fingerprint 73c5da0a', () => assert.equal(keys.masterFingerprint, 0x73c5da0a));

test('every account key/address matches the firmware export', () => {
  for (const a of MOCK.accounts) {
    const k = keys.accountKey(a.scheme, a.net, a.account);
    assert.equal(k.path, a.path, `${a.scheme} ${a.net} ${a.account} path`);
    if (a.kind === 'xpub') {
      assert.equal(k.kind, 'xpub');
      if (k.kind !== 'xpub') continue;
      assert.equal(bytesToHex(k.publicKey), a.publicKey, `${a.scheme} ${a.net} #${a.account} pubkey`);
      assert.equal(bytesToHex(k.chainCode), a.chainCode);
      assert.equal(k.masterFingerprint, a.masterFingerprint);
    } else {
      assert.equal(k.kind === 'address' && k.address, a.address, `${a.scheme} #${a.account}`);
    }
  }
});

test('every BTC address matches the firmware export', () => {
  for (const b of MOCK.btcAddresses) assert.equal(keys.btcAddress(b.scheme, b.net, b.account, b.change, b.index), b.address, `${b.scheme} ${b.net} ${b.account}/${b.change}/${b.index}`);
});
