// END TO END: the real app code (protocol client, PSBT builder, EVM/SOL helpers)
// talking over a real WebSocket to (1) the TypeScript simulator and (2) the REAL
// firmware running in the emulator (firmware/build-emu/tredder-emu.exe, if built).
import assert from 'node:assert/strict';
import { after, before, describe, test } from 'node:test';

import { ed25519 } from '@noble/curves/ed25519.js';
import { PublicKey, SystemProgram, TransactionInstruction, TransactionMessage, VersionedTransaction } from '@solana/web3.js';
import { Transaction as BtcTx, networks, payments } from 'bitcoinjs-lib';
import { encodeFunctionData, erc20Abi, hexToBytes, recoverTransactionAddress, serializeTransaction, type TransactionSerializableEIP1559 } from 'viem';

import { buildPsbt, finalizeSigned, planSend, type Utxo } from '../../app/src/chains/btc';
import { btcAddress, type XpubKey } from '../../app/src/chains/btcDerive';
import { signedEvmTx, type EvmPrepared } from '../../app/src/chains/evm';
import { DeviceClient } from '../../app/src/protocol/client';
import type { Scheme } from '../../app/src/protocol/types';
import { SimTransport } from '../../app/src/transport/sim';
import { TARGETS, startTarget, type Target } from './target';

const ABANDON = 'abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about';
const OLD = 'legal winner thank year wave sausage worth useful legal winner thank yellow';
const DEAD = '0x000000000000000000000000000000000000dEaD';

let c: DeviceClient;

const xpub = async (scheme: Scheme, wallet: 'active' | 'sweep-source' = 'active') => {
  const r = await c.request({ type: 'get-accounts', wallet, net: 'test', scheme, accounts: [0] });
  return r.accounts[0] as XpubKey;
};
const addr = async (scheme: Scheme, wallet: 'active' | 'sweep-source' = 'active') => {
  const r = await c.request({ type: 'get-accounts', wallet, net: 'test', scheme, accounts: [0] });
  const a = r.accounts[0]!;
  return a.kind === 'address' ? a.address : '';
};
const code = async (p: Promise<unknown>) => p.then(() => 'OK', (e: { code?: string }) => e.code ?? String(e));

async function btcPsbt(k: XpubKey, scheme: Scheme) {
  const address = btcAddress(k, scheme, 'test', 0, 2);
  const prev = new BtcTx();
  prev.addInput(new Uint8Array(32), 0);
  prev.addOutput(payments.p2pkh({ address: 'mipcBbFg9gMiCh81Kj8tqqdgoZub1ZJRfn', network: networks.testnet }).output!, 1n);
  prev.addOutput(payments.p2pkh({ address: 'mipcBbFg9gMiCh81Kj8tqqdgoZub1ZJRfn', network: networks.testnet }).output!, 50_000n);
  if (scheme === 'btc-p2pkh') prev.outs[1]!.script = payments.p2pkh({ address, network: networks.testnet }).output!;
  const utxo: Utxo = { txid: prev.getId(), vout: 1, value: 50_000n, confirmed: true, change: 0, index: 2, address };
  const plan = planSend({
    scheme, net: 'test', utxos: [utxo], to: 'tb1qw508d6qejxtdg4y5r3zarvary0c5xw7kxpjzsx', amount: 20_000n, feeRate: 2,
    changeAddress: btcAddress(k, scheme, 'test', 1, 0), changeIndex: 0,
  });
  return buildPsbt(k, scheme, 'test', plan, async () => prev.toBuffer());
}

const evmTx = (to: `0x${string}`, data?: `0x${string}`, chainId = 11155111): EvmPrepared => {
  const tx: TransactionSerializableEIP1559 = {
    type: 'eip1559', chainId, nonce: 3, to, value: data ? 0n : 10n ** 15n, data, gas: 60_000n, maxFeePerGas: 30n * 10n ** 9n, maxPriorityFeePerGas: 10n ** 9n,
  };
  return { chain: 'eth', net: 'test', tx, unsigned: hexToBytes(serializeTransaction(tx)), maxFee: 60_000n * 30n * 10n ** 9n };
};

const solMsg = (payer: string, ix: TransactionInstruction) =>
  new VersionedTransaction(
    new TransactionMessage({ payerKey: new PublicKey(payer), recentBlockhash: PublicKey.default.toBase58(), instructions: [ix] }).compileToV0Message(),
  );

for (const kind of TARGETS) {
  describe(kind === 'emu' ? 'REAL FIRMWARE in the emulator' : 'TypeScript simulator', () => {
    let target: Target;
    before(async () => {
      target = await startTarget(kind, { seed: ABANDON, sweepSeed: OLD, autoConfirm: true });
      c = new DeviceClient(await SimTransport.connect(`ws://localhost:${target.port}`));
    });
    after(async () => {
      await c.close();
      await target.close();
    });

    describe('accounts + receive', () => {
      test('get-info', async () => {
        assert.deepEqual(await c.request({ type: 'get-info' }), {
          ...target.info, protocol: 1, initialized: true, locked: false, hasSweepSource: true,
        });
      });
      test('accounts match the firmware-derived test wallet', async () => {
        assert.equal(await addr('evm-bip44'), '0x9858EfFD232B4033E47d90003D41EC34EcaEda94');
        assert.equal(await addr('sol-bip44-change'), 'HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk');
        assert.equal((await xpub('btc-p2wpkh')).masterFingerprint, 0x73c5da0a);
      });
      test('show-address equals the app watch-only derivation', async () => {
        const k = await xpub('btc-p2tr');
        const r = await c.request({ type: 'show-address', wallet: 'active', net: 'test', scheme: 'btc-p2tr', account: 0, change: 0, index: 9 });
        assert.equal(r.address, btcAddress(k, 'btc-p2tr', 'test', 0, 9));
      });
    });

    describe('Bitcoin signing (device signs, app verifies every signature)', () => {
      for (const scheme of ['btc-p2wpkh', 'btc-p2tr', 'btc-p2sh-p2wpkh', 'btc-p2pkh'] as Scheme[]) {
        test(scheme, async () => {
          const psbt = await btcPsbt(await xpub(scheme), scheme);
          const r = await c.request({ type: 'sign-btc', wallet: 'active', net: 'test', psbt: psbt.toBuffer() });
          const hex = finalizeSigned(r.psbt, 'test', psbt); // throws on any invalid signature
          assert.equal(BtcTx.fromHex(hex).ins.length, 1);
        });
      }
      test('refuses inputs from another wallet', async () => {
        const psbt = await btcPsbt(await xpub('btc-p2wpkh', 'sweep-source'), 'btc-p2wpkh');
        assert.equal(await code(c.request({ type: 'sign-btc', wallet: 'active', net: 'test', psbt: psbt.toBuffer() })), 'INVALID_REQUEST');
      });
    });

    describe('EVM signing', () => {
      test('native transfer: signature recovers to our address', async () => {
        const p = evmTx(DEAD);
        const sig = await c.request({ type: 'sign-evm', wallet: 'active', scheme: 'evm-bip44', account: 0, chainId: 11155111, tx: p.unsigned });
        assert.equal(await recoverTransactionAddress({ serializedTransaction: signedEvmTx(p, sig) as never }), '0x9858EfFD232B4033E47d90003D41EC34EcaEda94');
      });
      test('ERC-20 transfer and approve decode and sign', async () => {
        for (const data of [
          encodeFunctionData({ abi: erc20Abi, functionName: 'transfer', args: [DEAD, 5n] }),
          encodeFunctionData({ abi: erc20Abi, functionName: 'approve', args: [DEAD, 2n ** 256n - 1n] }),
        ]) {
          const p = evmTx('0x1c7D4B196Cb0C7B01d743Fbc6116a902379C7238', data);
          assert.equal(await code(c.request({ type: 'sign-evm', wallet: 'active', scheme: 'evm-bip44', account: 0, chainId: 11155111, tx: p.unsigned })), 'OK');
        }
      });
      test('undecodable contract call refused (blind signing off)', async () => {
        const p = evmTx(DEAD, '0xdeadbeef');
        assert.equal(await code(c.request({ type: 'sign-evm', wallet: 'active', scheme: 'evm-bip44', account: 0, chainId: 11155111, tx: p.unsigned })), 'UNSUPPORTED');
      });
      test('chainId in tx must match request', async () => {
        const p = evmTx(DEAD, undefined, 1);
        assert.equal(await code(c.request({ type: 'sign-evm', wallet: 'active', scheme: 'evm-bip44', account: 0, chainId: 11155111, tx: p.unsigned })), 'INVALID_REQUEST');
      });
    });

    describe('Solana signing', () => {
      test('SOL transfer: valid ed25519 signature by our key', async () => {
        const me = await addr('sol-bip44-change');
        const tx = solMsg(me, SystemProgram.transfer({ fromPubkey: new PublicKey(me), toPubkey: PublicKey.default, lamports: 1000 }));
        const msg = tx.message.serialize();
        const r = await c.request({ type: 'sign-sol', wallet: 'active', scheme: 'sol-bip44-change', account: 0, net: 'test', message: msg });
        assert.equal(r.publicKey, me);
        assert.ok(ed25519.verify(r.signature, msg, new PublicKey(me).toBytes()));
      });
      test('wrong fee payer and unknown programs refused', async () => {
        const other = await addr('sol-bip44-change', 'sweep-source');
        const bad = solMsg(other, SystemProgram.transfer({ fromPubkey: new PublicKey(other), toPubkey: PublicKey.default, lamports: 1 }));
        assert.equal(
          await code(c.request({ type: 'sign-sol', wallet: 'active', scheme: 'sol-bip44-change', account: 0, net: 'test', message: bad.message.serialize() })),
          'INVALID_REQUEST',
        );
        const me = await addr('sol-bip44-change');
        const unknown = solMsg(me, new TransactionInstruction({ programId: new PublicKey('Vote111111111111111111111111111111111111111'), keys: [], data: Buffer.from([1]) }));
        assert.equal(
          await code(c.request({ type: 'sign-sol', wallet: 'active', scheme: 'sol-bip44-change', account: 0, net: 'test', message: unknown.message.serialize() })),
          'UNSUPPORTED',
        );
      });
    });

    describe('sweep rules enforced by the device', () => {
      test('plan must pay only the device own new wallet', async () => {
        const step = { chain: 'Sepolia', asset: 'ETH', amount: 'all', feeEstimate: '0.001 ETH' };
        assert.equal(await code(c.request({ type: 'sweep-plan', plan: { id: 'x', steps: [{ ...step, to: DEAD }] } })), 'INVALID_REQUEST');
        const mine = await addr('evm-bip44');
        const ok = await c.request({ type: 'sweep-plan', plan: { id: 'p1', steps: [{ ...step, to: mine }] } });
        assert.equal(ok.approved, true);
      });
      test('sweep signatures must match an approved plan step', async () => {
        const mine = (await addr('evm-bip44')) as `0x${string}`;
        const base = { type: 'sign-evm', wallet: 'sweep-source', scheme: 'evm-bip44', account: 0, chainId: 11155111 } as const;
        assert.equal(await code(c.request({ ...base, tx: evmTx(mine).unsigned, planStep: 0 })), 'OK');
        assert.equal(await code(c.request({ ...base, tx: evmTx(DEAD).unsigned, planStep: 0 })), 'INVALID_REQUEST');
        assert.equal(await code(c.request({ ...base, tx: evmTx(mine).unsigned })), 'INVALID_REQUEST');
      });
      test('erase old seed', async () => {
        assert.deepEqual(await c.request({ type: 'forget-sweep-source' }), { erased: true });
        assert.equal((await c.request({ type: 'get-info' })).hasSweepSource, false);
      });
    });

    describe('access control', () => {
      test('locked and blank devices refuse', async () => {
        const locked = await startTarget(kind, { seed: ABANDON });
        const blank = await startTarget(kind);
        const cl = new DeviceClient(await SimTransport.connect(`ws://localhost:${locked.port}`));
        const cb = new DeviceClient(await SimTransport.connect(`ws://localhost:${blank.port}`));
        assert.equal(await code(cl.request({ type: 'get-accounts', wallet: 'active', net: 'test', scheme: 'evm-bip44', accounts: [0] })), 'LOCKED');
        assert.equal(await code(cb.request({ type: 'get-accounts', wallet: 'active', net: 'test', scheme: 'evm-bip44', accounts: [0] })), 'NOT_INITIALIZED');
        await cl.close();
        await cb.close();
        await locked.close();
        await blank.close();
      });
    });
  });
}
