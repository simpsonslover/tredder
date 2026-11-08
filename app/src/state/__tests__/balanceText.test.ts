import { balanceText } from '../WalletContext';

describe('balance text for the device screen', () => {
  it('uses only characters the device accepts, at most 19', () => {
    for (const v of [0, 0.5, 12.345, 1234.5, 999999.99, 1e7, 3.2e9, -5, NaN]) expect(balanceText(v)).toMatch(/^[0-9$.,~ kKmMbB-]{1,19}$/);
    expect(balanceText(1234.5)).toBe('$1,234.50');
    expect(balanceText(12_345_678)).toBe('$12.3M');
  });
});
