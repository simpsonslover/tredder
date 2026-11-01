// Integration (no network): pair with the mock device, load default accounts,
// verify receive addresses end to end through framing + CBOR + validation.
import { DeviceClient } from '../../protocol/client';
import { MockTransport } from '../../transport/mock';
import { DEFAULT_SCHEMES, fetchAccounts, verifyReceiveAddress, type Portfolio } from '../wallet';

const client = () => new DeviceClient(new MockTransport({ userDelayMs: 1 }));

describe('wallet flows against the mock device', () => {
  it('loads the default accounts', async () => {
    const c = client();
    const accts = await fetchAccounts(c, 'test', DEFAULT_SCHEMES, [0]);
    expect(accts.map((a) => a.scheme)).toEqual(DEFAULT_SCHEMES);
    expect(accts.find((a) => a.scheme === 'btc-p2wpkh')!.key).toMatchObject({ kind: 'xpub', path: "m/84'/1'/0'" });
    await c.close();
  });

  it('receive address is verified on device and must match local derivation', async () => {
    const c = client();
    const [btc, , evm, sol] = await fetchAccounts(c, 'test', DEFAULT_SCHEMES, [0]);
    const fakePortfolio = { btc: { [`${btc!.scheme}#0`]: { nextReceive: 7 } } } as unknown as Portfolio;
    const r = await verifyReceiveAddress(c, btc!, 'test', fakePortfolio);
    expect(r.index).toBe(7);
    expect(r.address).toMatch(/^tb1q/);
    expect((await verifyReceiveAddress(c, evm!, 'test', null)).address).toBe('0x9858EfFD232B4033E47d90003D41EC34EcaEda94');
    expect((await verifyReceiveAddress(c, sol!, 'test', null)).address).toBe('HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk');
    await c.close();
  });

  it('refuses a device address that differs from the account', async () => {
    const c = client();
    const [btc] = await fetchAccounts(c, 'test', ['btc-p2wpkh'], [0]);
    const [other] = await fetchAccounts(c, 'test', ['btc-p2wpkh'], [1]);
    // Account 0's key but ask for account 1's address: the app must notice.
    await expect(verifyReceiveAddress(c, { ...other!, key: btc!.key }, 'test', null)).rejects.toThrow(/different address/);
    await c.close();
  });
});
