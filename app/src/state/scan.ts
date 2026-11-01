// Import scan: look through every derivation scheme the popular wallets use,
// over the first few accounts, and report which ones hold funds or have history.
import type { Holding } from '../chains/types';
import type { DeviceClient } from '../protocol/client';
import { SCHEMES, type Net, type Scheme, type WalletRef } from '../protocol/types';
import { accountId, fetchAccounts, loadPortfolio, type Portfolio, type WalletAccount } from './wallet';

export interface FoundAccount {
  account: WalletAccount;
  holdings: Holding[];
  usd: number;
  used: boolean; // any balance or (BTC) any history
}

const RANGE: Record<Scheme, number[]> = {
  'btc-p2pkh': [0, 1, 2], 'btc-p2sh-p2wpkh': [0, 1, 2], 'btc-p2wpkh': [0, 1, 2], 'btc-p2tr': [0, 1, 2],
  'evm-bip44': [0, 1, 2, 3, 4], 'evm-ledger-live': [0, 1, 2, 3, 4],
  'sol-bip44-change': [0, 1, 2, 3, 4], 'sol-bip44': [0, 1, 2, 3, 4], 'sol-bip44-root': [0], 'sol-cli': [0],
};

export async function scanWallet(
  client: DeviceClient,
  net: Net,
  wallet: WalletRef,
  onProgress: (msg: string) => void = () => {},
): Promise<{ found: FoundAccount[]; portfolio: Portfolio; accounts: WalletAccount[] }> {
  const accounts: WalletAccount[] = [];
  for (const scheme of SCHEMES) {
    onProgress(`Reading ${scheme} accounts from device…`);
    accounts.push(...(await fetchAccounts(client, net, [scheme], RANGE[scheme], wallet)));
  }
  onProgress('Checking balances on every chain…');
  const portfolio = await loadPortfolio(accounts, net, { btcGap: 10 });
  const found = accounts.map((a): FoundAccount => {
    const holdings = portfolio.holdings.filter((h) => h.scheme === a.scheme && h.account === a.account && h.amount > 0n);
    const btcUsed = (portfolio.btc[accountId(a)]?.usedAddresses.length ?? 0) > 0;
    return { account: a, holdings, usd: holdings.reduce((s, h) => s + (h.usdValue ?? 0), 0), used: holdings.length > 0 || btcUsed };
  });
  return { found, portfolio, accounts };
}
