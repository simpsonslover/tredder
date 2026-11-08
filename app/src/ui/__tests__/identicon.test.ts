import { assetLogo, assetSeed } from '../coinLogos';
import { identiconPattern } from '../Identicon';

describe('generated token icons', () => {
  const tok = (address: string) => ({ chain: 'eth' as const, asset: { kind: 'token' as const, address, standard: 'erc20' as const } });

  it('is deterministic, mirrored, never empty', () => {
    const a = identiconPattern('eth:0xe717eeb40af968eadbe9e29a7904be19105e1e81');
    expect(identiconPattern('eth:0xe717eeb40af968eadbe9e29a7904be19105e1e81')).toEqual(a);
    expect(a.cells).toHaveLength(25);
    for (let r = 0; r < 5; r++) {
      expect(a.cells[r * 5]).toBe(a.cells[r * 5 + 4]);
      expect(a.cells[r * 5 + 1]).toBe(a.cells[r * 5 + 3]);
    }
    expect(a.cells.some(Boolean)).toBe(true);
    expect(a.fg).toMatch(/^#[0-9a-f]{6}$/);
  });

  it('two "USDT" clones from different contracts look different', () => {
    const clones = ['0xe717eEB40aF968EaDBE9E29a7904be19105E1e81', '0xaA8E23Fb1079EA71e0a56F48a2aA51851D8433D0', '0xd077A400968890Eacc75cdc901F0356c943e4fDb', '0x186cca6904490818AB0DC409ca59D932A2366031'];
    const looks = clones.map((c) => JSON.stringify(identiconPattern(assetSeed(tok(c))!)));
    expect(new Set(looks).size).toBe(clones.length);
  });

  it('verified tokens keep their real logo; the seed ignores address case', () => {
    expect(assetLogo(tok('0x1c7D4B196Cb0C7B01d743Fbc6116a902379C7238'))).toBeDefined(); // Circle USDC, Sepolia
    expect(assetLogo(tok('0xe717eEB40aF968EaDBE9E29a7904be19105E1e81'))).toBeUndefined(); // a Sepolia "Tether USDT"
    expect(assetSeed(tok('0xABC'))).toBe(assetSeed(tok('0xabc')));
    expect(assetSeed({ chain: 'eth', asset: { kind: 'native' } })).toBeUndefined();
  });
});
