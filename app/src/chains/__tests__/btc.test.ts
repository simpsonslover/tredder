import { bytesToHex, hexToBytes } from '@noble/hashes/utils.js';
import { Transaction, networks, payments } from 'bitcoinjs-lib';

import { MOCK as data } from '../../dev/mockData';
import type { Scheme } from '../../protocol/types';
import { buildPsbt, DUST, planSend, type Utxo } from '../btc';
import { btcAddress, type XpubKey } from '../btcDerive';

const key = (scheme: Scheme, net: 'main' | 'test' = 'test', account = 0): XpubKey => {
  const k = data.accounts.find((a) => a.scheme === scheme && a.net === net && a.account === account);
  if (!k || k.kind !== 'xpub') throw new Error('no key');
  return { account, path: k.path, kind: 'xpub', publicKey: hexToBytes(k.publicKey), chainCode: hexToBytes(k.chainCode), masterFingerprint: k.masterFingerprint };
};

const utxo = (k: XpubKey, scheme: Scheme, value: bigint, index: number, txid = 'aa'.repeat(32)): Utxo => ({
  txid, vout: index, value, confirmed: true, change: 0, index, address: btcAddress(k, scheme, 'test', 0, index),
});

const RECIPIENT = 'tb1qw508d6qejxtdg4y5r3zarvary0c5xw7kxpjzsx'; // BIP173 testnet vector

describe('planSend', () => {
  const k = key('btc-p2wpkh');
  const changeAddress = btcAddress(k, 'btc-p2wpkh', 'test', 1, 0);
  const common = { scheme: 'btc-p2wpkh' as const, net: 'test' as const, to: RECIPIENT, feeRate: 2, changeAddress, changeIndex: 0 };

  it('selects largest first and returns change', () => {
    const p = planSend({ ...common, utxos: [utxo(k, 'btc-p2wpkh', 10_000n, 0), utxo(k, 'btc-p2wpkh', 50_000n, 1)], amount: 20_000n });
    expect(p.inputs.map((i) => i.value)).toEqual([50_000n]);
    expect(p.outputs[0]).toEqual({ address: RECIPIENT, value: 20_000n });
    expect(p.outputs[1]).toMatchObject({ address: changeAddress, change: { index: 0 } });
    expect(p.inputs[0]!.value).toBe(p.outputs.reduce((s, o) => s + o.value, 0n) + p.fee);
  });

  it('gives dust change to the fee instead of creating a dust output', () => {
    const p = planSend({ ...common, utxos: [utxo(k, 'btc-p2wpkh', 20_500n, 0)], amount: 20_000n });
    expect(p.outputs).toHaveLength(1);
    expect(p.fee).toBe(500n);
  });

  it('max sends everything minus fee', () => {
    const p = planSend({ ...common, utxos: [utxo(k, 'btc-p2wpkh', 10_000n, 0), utxo(k, 'btc-p2wpkh', 5_000n, 1)], amount: 'max' });
    expect(p.inputs).toHaveLength(2);
    expect(p.outputs[0]!.value + p.fee).toBe(15_000n);
  });

  it('rejects insufficient funds, dust amounts and wrong-network recipients', () => {
    expect(() => planSend({ ...common, utxos: [utxo(k, 'btc-p2wpkh', 1_000n, 0)], amount: 900n })).toThrow(/Insufficient/);
    expect(() => planSend({ ...common, utxos: [utxo(k, 'btc-p2wpkh', 100_000n, 0)], amount: DUST - 1n })).toThrow(/dust/);
    expect(() => planSend({ ...common, to: 'bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu', utxos: [utxo(k, 'btc-p2wpkh', 100_000n, 0)], amount: 1000n })).toThrow();
  });
});

describe('buildPsbt: everything the device needs to verify', () => {
  const fakePrev = (k: XpubKey, scheme: Scheme) => async (txid: string) => {
    // a fake previous tx paying the p2pkh address at the right vout
    const tx = new Transaction();
    tx.addInput(new Uint8Array(32), 0);
    for (let i = 0; i <= 1; i++)
      tx.addOutput(payments.p2pkh({ address: btcAddress(k, scheme, 'test', 0, i), network: networks.testnet }).output!, 10_000n);
    expect(txid).toBeDefined();
    return tx.toBuffer();
  };

  it.each(['btc-p2wpkh', 'btc-p2sh-p2wpkh', 'btc-p2tr', 'btc-p2pkh'] as Scheme[])('%s', async (scheme) => {
    const k = key(scheme);
    const changeAddress = btcAddress(k, scheme, 'test', 1, 3);
    let utxos = [utxo(k, scheme, 10_000n, 1)];
    if (scheme === 'btc-p2pkh') {
      const prev = await fakePrev(k, scheme)('x');
      const id = Transaction.fromBuffer(prev).getId();
      utxos = [{ ...utxos[0]!, txid: id }];
    }
    const plan = planSend({ scheme, net: 'test', utxos, to: RECIPIENT, amount: 3_000n, feeRate: 1, changeAddress, changeIndex: 3 });
    const psbt = await buildPsbt(k, scheme, 'test', plan, fakePrev(k, scheme));
    const inp = psbt.data.inputs[0]!;
    const fp = bytesToHex(new Uint8Array([0x73, 0xc5, 0xda, 0x0a]));
    if (scheme === 'btc-p2tr') {
      expect(bytesToHex(inp.tapBip32Derivation![0]!.masterFingerprint)).toBe(fp);
      expect(inp.tapBip32Derivation![0]!.path).toBe(`${k.path}/0/1`);
      expect(inp.tapInternalKey).toHaveLength(32);
    } else {
      expect(bytesToHex(inp.bip32Derivation![0]!.masterFingerprint)).toBe(fp);
      expect(inp.bip32Derivation![0]!.path).toBe(`${k.path}/0/1`);
    }
    if (scheme === 'btc-p2pkh') expect(inp.nonWitnessUtxo).toBeDefined();
    else expect(inp.witnessUtxo!.value).toBe(10_000n);
    if (scheme === 'btc-p2sh-p2wpkh') expect(inp.redeemScript).toBeDefined();
    // change output carries its key origin so the device can prove it is its own
    const change = psbt.data.outputs[1]!;
    const path = scheme === 'btc-p2tr' ? change.tapBip32Derivation![0]!.path : change.bip32Derivation![0]!.path;
    expect(path).toBe(`${k.path}/1/3`);
    expect(psbt.data.outputs[0]!.bip32Derivation).toBeUndefined();
  });
});
