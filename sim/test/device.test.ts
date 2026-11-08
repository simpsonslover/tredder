// Button-level tests of the simulated device UI.
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { Device } from '../src/device';
import { WalletKeys } from '../src/keys';

const ABANDON = 'abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about';
const typeAll = (d: Device, s: string) => [...s].forEach((ch) => d.type(ch));

/** Type a word's first letters in the picker; accept it if not auto-accepted. */
function enterWord(d: Device, w: string) {
  typeAll(d, w.slice(0, 4));
  if (/^Word/.test(d.view().title) && d.view().items?.[0]?.startsWith('✓')) d.press('confirm');
}

test('unlock with PIN', () => {
  const d = new Device({ seed: ABANDON, pin: '2580' });
  assert.equal(d.status().locked, true);
  d.press('confirm'); // Unlock
  assert.match(d.view().title, /Enter PIN/);
  typeAll(d, '2580');
  d.type('Enter');
  assert.equal(d.status().locked, false);
});

test('PIN pad keys stay put, the cursor start is random', () => {
  const d = new Device({ seed: ABANDON });
  d.press('confirm');
  const orders = new Set<string>();
  const cursors = new Set<number>();
  for (let i = 0; i < 8; i++) {
    d.type('1');
    orders.add(d.view().items!.slice(0, 10).join(''));
    cursors.add(d.view().cursor!);
  }
  assert.equal(orders.size, 1, 'digit order must not change');
  assert.ok(cursors.size > 1, 'cursor start should vary');
});

test('10 wrong PINs wipe the device', () => {
  const d = new Device({ seed: ABANDON, pin: '1234', pinDelayMs: 0 });
  for (let i = 0; i < 10; i++) {
    d.press('confirm'); // Unlock
    assert.match(d.view().title, /Enter PIN/);
    typeAll(d, '9999');
    d.type('Enter');
    if (/Wrong PIN/.test(d.view().title)) d.press('confirm');
  }
  assert.equal(d.status().initialized, false);
  assert.match(d.view().title, /Welcome/);
});

test('import 12 words with the letter picker, checksum checked, then set PIN', () => {
  const d = new Device();
  d.press('down'); // Import seed phrase
  d.press('confirm');
  d.press('confirm'); // 12 words
  for (const w of ABANDON.split(' ')) enterWord(d, w);
  assert.match(d.view().title, /Review words/);
  assert.equal(d.view().items!.length, 13);
  for (let i = 0; i < 12; i++) d.press('down');
  d.press('confirm'); // ✓ Words are correct
  assert.match(d.view().title, /ever typed/);
  d.press('down'); // Import as-is
  d.press('confirm');
  typeAll(d, '4321');
  d.type('Enter');
  typeAll(d, '4321');
  d.type('Enter');
  assert.deepEqual(d.status(), { locked: false, initialized: true, paired: false, hasSweepSource: false, failedPins: 0 });
});

test('wrong checksum is caught and a word can be fixed', () => {
  const d = new Device();
  d.press('down');
  d.press('confirm');
  d.press('confirm');
  const words = ABANDON.split(' ');
  words[11] = 'abandon'; // checksum now wrong
  for (const w of words) enterWord(d, w);
  assert.match(d.view().title, /Checksum wrong/);
  for (let i = 0; i < 11; i++) d.press('down');
  d.press('confirm'); // edit word 12
  enterWord(d, 'about');
  assert.match(d.view().title, /Review words/);
});

test('create wallet: shows words, passes the 3-word quiz, sets PIN', () => {
  const d = new Device();
  d.press('confirm'); // Create new wallet
  d.press('confirm'); // 12 words
  const words: string[] = [];
  while (/Write down/.test(d.view().title)) {
    for (const l of d.view().lines) {
      const [n, w] = l.split('. ');
      words[Number(n) - 1] = w!;
    }
    d.press('confirm');
  }
  d.press('confirm'); // "Keep it safe" -> quiz
  for (let k = 0; k < 3; k++) {
    const pos = Number(/word #(\d+)/.exec(d.view().lines[0]!)![1]);
    const idx = d.view().items!.indexOf(words[pos - 1]!);
    for (let i = 0; i < idx; i++) d.press('down');
    d.press('confirm');
  }
  typeAll(d, '1111');
  d.type('Enter');
  typeAll(d, '1111');
  d.type('Enter');
  assert.equal(d.status().initialized, true);
  assert.equal(words.length, 12);
  assert.doesNotThrow(() => new WalletKeys(words.join(' ')));
});
