// The REAL firmware's screens and flows, driven only through the four emulated
// buttons (debug link -> robot finger), exactly as a person would use the device.
// Skipped if the emulator has not been built (python firmware/tools/build_emu.py).
import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { describe, test } from 'node:test';

import { DeviceClient } from '../../app/src/protocol/client';
import { SimTransport } from '../../app/src/transport/sim';
import { DebugLink, TARGETS, startEmu, waitFor, type Screen } from './target';

const ABANDON = 'abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about';
const skip = !TARGETS.includes('emu') && 'emulator not built';

async function enterPin(d: DebugLink, pin: string) {
  await waitFor(d, (s) => s.kind === 'pin');
  await d.type(pin);
  await d.enter();
}

async function enterWord(d: DebugLink, w: string) {
  await d.type(w.slice(0, 4));
  const s = await d.screen();
  if (s.kind === 'word' && s.items[0]?.startsWith('✓')) await d.enter(); // 3-letter words: pick explicitly
}

async function selectItem(d: DebugLink, label: string | RegExp) {
  const s = await d.screen();
  const idx = s.items.findIndex((i) => (typeof label === 'string' ? i === label : label.test(i)));
  assert.ok(idx >= 0, `no item ${label} in ${JSON.stringify(s.items)}`);
  const n = s.items.length;
  const down = (idx - s.cursor + n) % n;
  for (let i = 0; i < down; i++) await d.press('down');
  await d.press('ok');
}

/** Press OK through every page of a page sequence. */
async function pageThrough(d: DebugLink): Promise<Screen> {
  const s = await waitFor(d, (x) => x.kind === 'pages');
  for (let i = 0; i < s.npages; i++) await d.press('ok');
  return d.screen();
}

describe('REAL FIRMWARE: on-device flows (buttons only)', { skip }, () => {
  test('unlock with PIN', async () => {
    const t = await startEmu({ seed: ABANDON, pin: '2580' });
    const d = t.debug!;
    assert.equal((await d.screen()).locked, true);
    await d.press('ok'); // Unlock
    const s = await waitFor(d, (x) => x.kind === 'pin');
    assert.equal(s.title, 'Enter PIN');
    await enterPin(d, '2580');
    await waitFor(d, (x) => !x.locked && x.title.startsWith('tredder'));
    await t.close();
  });

  test('PIN pad: keys never move, the cursor jumps to a random digit after every entry', async () => {
    const t = await startEmu({ seed: ABANDON });
    const d = t.debug!;
    await d.press('ok');
    const orders = new Set<string>();
    const cursors = new Set<number>();
    for (let i = 0; i < 8; i++) {
      const s = await waitFor(d, (x) => x.kind === 'pin');
      orders.add(s.items.slice(0, 10).join(''));
      cursors.add(s.cursor);
      await d.type('1');
    }
    assert.equal(orders.size, 1, 'digit order must not change');
    assert.ok(cursors.size > 1, 'cursor start should vary');
    await t.close();
  });

  test('10 wrong PINs: secure element erases its secret, device wipes', async () => {
    const t = await startEmu({ seed: ABANDON, pin: '1234', extraArgs: ['--pin-delay-ms', '0'] });
    const d = t.debug!;
    for (let i = 1; i <= 10; i++) {
      await waitFor(d, (x) => x.kind === 'menu' && x.items[0] === 'Unlock');
      await d.press('ok');
      await enterPin(d, '9999');
      const s = await waitFor(d, (x) => x.kind === 'pages');
      if (i < 10) {
        assert.equal(s.title, 'Wrong PIN');
        assert.equal(s.triesLeft, 10 - i);
      } else {
        assert.equal(s.title, 'Device wiped');
      }
      await d.press('ok');
    }
    const s = await waitFor(d, (x) => x.title === 'Setup');
    assert.equal(s.initialized, false);
    await t.close();
  });

  test('import 12 words with the letter picker, choose PIN, pair the app on the device', async () => {
    const t = await startEmu();
    const d = t.debug!;
    await selectItem(d, 'Import wallet');
    await selectItem(d, '12 words');
    for (const w of ABANDON.split(' ')) await enterWord(d, w);
    let s = await waitFor(d, (x) => x.title === 'Review words');
    assert.equal(s.items.length, 13);
    await selectItem(d, '✓ Words are correct');
    await waitFor(d, (x) => x.title === 'Was it ever typed?');
    await selectItem(d, 'Import as-is');
    await enterPin(d, '4321');
    await waitFor(d, (x) => x.title === 'Repeat PIN');
    await enterPin(d, '4321');
    s = await pageThrough(d); // "Ready"
    assert.equal(s.initialized, true);
    assert.equal(s.locked, false);

    // First request from the app: pairing prompt + PIN on the device.
    const c = new DeviceClient(await SimTransport.connect(`ws://localhost:${t.port}`));
    const r = c.request({ type: 'get-accounts', wallet: 'active', net: 'test', scheme: 'evm-bip44', accounts: [0] });
    s = await waitFor(d, (x) => x.kind === 'pages' && x.title === 'Allow pairing?');
    assert.ok(s.lines.some((l) => /^\d{6}$/.test(l)), 'pairing code shown');
    await d.press('ok');
    await enterPin(d, '4321');
    const accounts = await r;
    const first = accounts.accounts[0]!;
    assert.equal(first.kind === 'address' && first.address, '0x9858EfFD232B4033E47d90003D41EC34EcaEda94');
    await c.close();
    await t.close();
  });

  test('wrong checksum is caught and one word can be fixed', async () => {
    const t = await startEmu();
    const d = t.debug!;
    await selectItem(d, 'Import wallet');
    await selectItem(d, '12 words');
    const words = ABANDON.split(' ');
    words[11] = 'abandon';
    for (const w of words) await enterWord(d, w);
    await waitFor(d, (x) => x.title === 'Checksum wrong!');
    await selectItem(d, '12. abandon');
    await enterWord(d, 'about');
    const s = await waitFor(d, (x) => x.title === 'Review words');
    assert.equal(s.items.at(-1), '✓ Words are correct');
    await t.close();
  });

  test('create wallet: write down words, pass the 3-word check, set PIN', async () => {
    const t = await startEmu();
    const d = t.debug!;
    await selectItem(d, 'New wallet');
    await selectItem(d, '12 words');
    const words: string[] = [];
    let s = await waitFor(d, (x) => x.kind === 'pages');
    for (;;) {
      for (const l of s.lines) {
        const m = /^\s*(\d+)\. (\w+)$/.exec(l);
        if (m) words[Number(m[1]) - 1] = m[2]!;
      }
      const last = s.page === s.npages - 1;
      await d.press('ok');
      if (last) break;
      s = await d.screen();
    }
    assert.equal(words.filter(Boolean).length, 12);
    for (let k = 0; k < 3; k++) {
      s = await waitFor(d, (x) => x.title === `Check ${k + 1} of 3`);
      const pos = Number(/word #(\d+)/.exec(s.lines[0]!)![1]);
      await selectItem(d, words[pos - 1]!);
    }
    await enterPin(d, '1111');
    await enterPin(d, '1111');
    s = await pageThrough(d);
    assert.equal(s.initialized, true);
    await t.close();
  });

  test('sleep after idle: locks, display off; the waking press does nothing else', async () => {
    const t = await startEmu({ seed: ABANDON, pin: '1234', extraArgs: ['--sleep-ms', '1500'] });
    const d = t.debug!;
    await d.press('ok');
    await enterPin(d, '1234');
    await waitFor(d, (x) => !x.locked && x.title.startsWith('tredder'));
    const s = await waitFor(d, (x) => x.sleeping, 8000);
    assert.equal(s.locked, true, 'sleeping wipes the seed from RAM (locked)');
    await d.press('ok'); // wakes the device only
    let w = await waitFor(d, (x) => !x.sleeping);
    assert.equal(w.kind, 'menu', 'the wake-up press must not act (no PIN pad yet)');
    assert.equal(w.items[0], 'Unlock');
    await d.press('ok'); // now a real press
    w = await waitFor(d, (x) => x.kind === 'pin');
    assert.equal(w.title, 'Enter PIN');
    await t.close();
  });

  test('battery low on battery power: warning, then sleep', async () => {
    const t = await startEmu({ seed: ABANDON, pin: '1234', extraArgs: ['--battery', '0'] });
    const d = t.debug!;
    const s = await waitFor(d, (x) => x.title === 'Battery low', 8000);
    assert.ok(s.batteryMv < 3350, `VBAT ${s.batteryMv} mV`);
    await waitFor(d, (x) => x.sleeping, 20_000);
    await t.close();
  });

  test('power cycle: encrypted flash keeps the wallet, the SE keeps the wrong-PIN count', async () => {
    const dir = mkdtempSync(join(tmpdir(), 'tredder-emu-'));
    try {
      let t = await startEmu({ seed: ABANDON, pin: '1234', stateDir: dir });
      // the emulator creates the wallet (and writes flash.bin) after it starts: wait for it, or the
      // test can stop it first (that was the flaky failure)
      await waitFor(t.debug!, (x) => x.initialized);
      await t.close();
      // the emulator writes flash.bin while it shuts down: wait for it (was flaky)
      for (let i = 0; i < 50 && !existsSync(join(dir, 'flash.bin')); i++) await new Promise((ok) => setTimeout(ok, 100));
      const flash = readFileSync(join(dir, 'flash.bin'));
      assert.ok(!flash.includes(Buffer.from('abandon')), 'no plaintext words in flash');
      assert.ok(!flash.includes(Buffer.alloc(24, 0)), 'word indices (abandon = 0) are not stored in clear');

      t = await startEmu({ stateDir: dir }); // "power on" with the same chips
      let d = t.debug!;
      let s = await d.screen();
      assert.equal(s.initialized, true);
      assert.equal(s.locked, true);
      await d.press('ok');
      await enterPin(d, '0000');
      s = await waitFor(d, (x) => x.title === 'Wrong PIN');
      await t.close(); // pull the power right after a wrong attempt

      t = await startEmu({ stateDir: dir });
      d = t.debug!;
      s = await d.screen();
      assert.equal(s.triesLeft, 9, 'secure element counter survived the power cycle');
      await d.press('ok');
      await enterPin(d, '1234');
      s = await waitFor(d, (x) => !x.locked);
      assert.equal(s.triesLeft, 10, 'correct PIN resets the counter');
      await t.close();
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });
});
