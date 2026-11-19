// Demo portfolio for screenshots only. Active when the app is built with
// EXPO_PUBLIC_DEMO_PORTFOLIO=1 (the bundler inlines the variable, so normal builds drop this path).
// Fake balances, no device, no network requests.
import type { Holding } from '../chains/types';
import type { Portfolio, WalletAccount } from '../state/wallet';

export const DEMO_PORTFOLIO_ENABLED = process.env.EXPO_PUBLIC_DEMO_PORTFOLIO === '1';

const h = (chain: Holding['chain'], symbol: string, name: string, decimals: number, amount: number, price: number,
  asset: Holding['asset'] = { kind: 'native' }): Holding => {
  const units = BigInt(Math.round(amount * 10 ** Math.min(decimals, 8))) * 10n ** BigInt(Math.max(decimals - 8, 0));
  return { chain, net: 'main', scheme: chain === 'btc' ? 'btc-p2wpkh' : chain === 'sol' ? 'sol-bip44-change' : 'evm-bip44',
    account: 0, owner: 'demo', asset, kind: asset.kind === 'native' ? 'native' : 'token', symbol, name, decimals,
    amount: units, usdPrice: price, usdValue: amount * price };
};
const erc20 = (address: string): Holding['asset'] => ({ kind: 'token', address, standard: 'erc20' });
const spl = (address: string): Holding['asset'] => ({ kind: 'token', address, standard: 'spl' });

const holdings: Holding[] = [
  h('btc', 'BTC', 'Bitcoin', 8, 0.4218, 68250),
  h('eth', 'ETH', 'Ether', 18, 3.215, 3420),
  h('sol', 'SOL', 'Solana', 9, 48.6, 172.4),
  h('base', 'USDC', 'USD Coin', 6, 1250, 1, erc20('0x833589fCD6eDb6E08f4c7C32D4f71b54bdA02913')),
  h('eth', 'USDT', 'Tether USD', 6, 820.5, 1, erc20('0xdAC17F958D2ee523a2206206994597C13D831ec7')),
  h('eth', 'LINK', 'Chainlink', 18, 64.2, 14.85, erc20('0x514910771af9ca656af840dff83e8264ecf986ca')),
  h('arbitrum', 'ETH', 'Ether', 18, 0.412, 3420),
  h('sol', 'USDC', 'USD Coin', 6, 310, 1, spl('EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v')),
  h('eth', 'UNI', 'Uniswap', 18, 37.5, 9.12, erc20('0x1f9840a85d5af5bf1d1762f925bdaddc4201f984')),
];

export const DEMO_PORTFOLIO: Portfolio = {
  holdings, btc: {}, errors: [], updatedAt: Date.now(),
  totalUsd: holdings.reduce((s, x) => s + (x.usdValue ?? 0), 0),
};
export const DEMO_ACCOUNTS = [{ family: 'btc', scheme: 'btc-p2wpkh', account: 0, key: { kind: 'address', address: 'demo' } }] as unknown as WalletAccount[];
