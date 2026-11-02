import { loadRecents, rememberRecipient } from '../../state/recents';
import type { KV } from '../../state/storage';
import { coinToUsd, pressKey, unitsToInput, unitsToNumber, usdToCoin } from '../amountInput';
import { addressParts, checkRecipient, parseRecipientInput, shortAddress } from '../recipient';

// BIP-173 test vectors (same witness program on both networks) and EIP-55 vectors
const BTC_TEST = 'tb1qw508d6qejxtdg4y5r3zarvary0c5xw7kxpjzsx';
const BTC_MAIN = 'bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4';
const EVM = '0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed';
const SOL = 'So11111111111111111111111111111111111111112';

describe('recipient input', () => {
  it('reads plain addresses and payment URIs', () => {
    expect(parseRecipientInput(`  ${EVM}\n`)).toEqual({ address: EVM });
    expect(parseRecipientInput(`bitcoin:${BTC_TEST}?amount=0.015&label=x`)).toEqual({ address: BTC_TEST, amount: '0.015', scheme: 'btc' });
    expect(parseRecipientInput(`ethereum:${EVM}@11155111?value=2500000000000000000`)).toEqual({ address: EVM, amount: '2.5', scheme: 'evm' });
    expect(parseRecipientInput(`solana:${SOL}?amount=0.5`)).toEqual({ address: SOL, amount: '0.5', scheme: 'sol' });
  });
  it('refuses contract-call URIs and odd amounts', () => {
    expect(parseRecipientInput(`ethereum:0xToken/transfer?address=${EVM}&uint256=1`).address).toBe('');
    expect(parseRecipientInput(`bitcoin:${BTC_TEST}?amount=1e5`).amount).toBeUndefined();
  });
});

describe('recipient check', () => {
  it('accepts the right network', () => {
    expect(checkRecipient('btc', 'test', BTC_TEST)).toEqual({ ok: true, address: BTC_TEST });
    expect(checkRecipient('btc', 'main', BTC_MAIN)).toEqual({ ok: true, address: BTC_MAIN });
    expect(checkRecipient('eth', 'test', EVM.toLowerCase())).toEqual({ ok: true, address: EVM }); // normalised to checksum
    expect(checkRecipient('sol', 'test', SOL)).toEqual({ ok: true, address: SOL });
  });
  it('explains what is wrong', () => {
    const r = (c: Parameters<typeof checkRecipient>[0], n: 'main' | 'test', a: string) => {
      const x = checkRecipient(c, n, a);
      return x.ok ? 'ok' : x.reason;
    };
    expect(r('btc', 'test', BTC_MAIN)).toMatch(/mainnet Bitcoin address/);
    expect(r('btc', 'main', BTC_TEST)).toMatch(/testnet Bitcoin address/);
    expect(r('btc', 'test', EVM)).toMatch(/EVM address, not a Bitcoin/);
    expect(r('sol', 'test', EVM)).toMatch(/EVM address, not a Solana/);
    expect(r('eth', 'test', SOL)).toMatch(/Solana address/);
    expect(r('eth', 'test', EVM.slice(0, -1) + 'D')).toMatch(/Checksum/); // one letter's case changed
    expect(r('eth', 'test', '0x1234')).toBe('Not a valid address');
    expect(r('sol', 'test', '')).toBe('Enter an address');
  });
  it('shortens for display', () => {
    expect(shortAddress(EVM)).toBe('0x5aAe…eAed');
    expect(shortAddress(BTC_TEST, 6)).toBe('tb1qw5…xpjzsx');
    expect(addressParts(SOL)).toEqual(['So1111', SOL.slice(6, -6), '111112']);
  });
});

describe('amount keypad', () => {
  const type = (keys: string, d: number) => [...keys].reduce((s, k) => pressKey(s, (k === '<' ? 'del' : k) as never, d), '');
  it('types like a calculator', () => {
    expect(type('0.5', 9)).toBe('0.5');
    expect(type('.5', 9)).toBe('0.5');
    expect(type('05', 9)).toBe('5');
    expect(type('1..2', 9)).toBe('1.2');
    expect(type('12<3', 9)).toBe('13');
  });
  it('never allows more decimals than the coin has', () => {
    expect(type('1.123456789', 6)).toBe('1.123456');
    expect(type('9.99', 0)).toBe('999');
    expect(type('3.14159', 2)).toBe('3.14');
  });
  it('converts USD both ways, rounding the coin amount down', () => {
    expect(usdToCoin('100', 50_000, 8)).toBe('0.002');
    expect(usdToCoin('1', 3, 6)).toBe('0.333333');
    expect(usdToCoin('0', 3, 6)).toBe('');
    expect(coinToUsd('0.5', 3000)).toBe('1500.00');
    expect(coinToUsd('1', undefined)).toBe('');
  });
  it('handles big balances (no thousands separators in maths)', () => {
    expect(unitsToNumber(1_234_500_000_000n, 9)).toBe(1234.5);
    expect(unitsToInput(1_234_500_000_000n, 9)).toBe('1234.5');
  });
});

describe('recent recipients', () => {
  const mem = (): KV => {
    const m = new Map<string, string>();
    return { get: async (k) => m.get(k) ?? null, set: async (k, v) => void m.set(k, v), del: async (k) => void m.delete(k) };
  };
  it('keeps newest first, counts repeats, matches EVM case-insensitively', async () => {
    const kv = mem();
    await rememberRecipient('evm', 'test', EVM, kv, 1);
    await rememberRecipient('sol', 'test', SOL, kv, 2);
    const r = await rememberRecipient('evm', 'test', EVM.toLowerCase(), kv, 3);
    expect(r.map((x) => [x.family, x.count, x.last])).toEqual([
      ['evm', 2, 3],
      ['sol', 1, 2],
    ]);
    expect(await loadRecents(kv)).toHaveLength(2);
  });
});
