import { assetLogo, chainLogo } from '../coinLogos';

const token = (chain: 'eth' | 'sol' | 'base', address: string) => ({ chain, asset: { kind: 'token' as const, address, standard: chain === 'sol' ? ('spl' as const) : ('erc20' as const) } });

describe('coin logos', () => {
  test('native coins have logos on every chain', () => {
    for (const chain of ['btc', 'eth', 'base', 'arbitrum', 'bnb', 'polygon', 'sol'] as const) {
      expect(assetLogo({ chain, asset: { kind: 'native' } })).toBeDefined();
    }
  });

  test('known token contract gets its logo (EVM address case does not matter)', () => {
    expect(assetLogo(token('eth', '0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48'))).toBeDefined();
    expect(assetLogo(token('sol', 'EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v'))).toBeDefined();
  });

  test('a token NAMED like a real one but at another address gets no logo (anti-spoofing)', () => {
    expect(assetLogo(token('eth', '0x000000000000000000000000000000000000dead'))).toBeUndefined();
    // the real Ethereum USDC address on another chain is not that chain's USDC
    expect(assetLogo(token('base', '0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48'))).toBeUndefined();
    // Solana mints are case-sensitive
    expect(assetLogo(token('sol', 'epjfwdd5aufqssqem2qn1xzybapc8g4wegGkzwytdt1v'))).toBeUndefined();
  });

  test('Base and Arbitrum have no CC0 chain logo (letter badge)', () => {
    expect(chainLogo('base')).toBeUndefined();
    expect(chainLogo('arbitrum')).toBeUndefined();
    expect(chainLogo('eth')).toBeDefined();
  });
});
