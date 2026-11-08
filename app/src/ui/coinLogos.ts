/*
 * Real coin logos, bundled with the app (never loaded from a server at runtime:
 * fetching icons would tell a third party which assets this wallet holds).
 * Source: cryptocurrency-icons 0.18.1, CC0-1.0 (assets/coins/LICENSE.md).
 *
 * A token only gets a logo if its CONTRACT ADDRESS is on the list below.
 * Anyone can deploy a token called "USDC"; matching by symbol would dress a
 * scam token up as the real one. Unknown tokens keep the letter badge.
 */
import type { ImageSourcePropType } from 'react-native';

import type { ChainId } from '../chains/config';
import type { Holding } from '../chains/types';

const L = {
  btc: require('../../assets/coins/btc.png'),
  eth: require('../../assets/coins/eth.png'),
  sol: require('../../assets/coins/sol.png'),
  bnb: require('../../assets/coins/bnb.png'),
  matic: require('../../assets/coins/matic.png'), // pre-2024 Polygon mark; the pack has no POL logo
  usdc: require('../../assets/coins/usdc.png'),
  usdt: require('../../assets/coins/usdt.png'),
  dai: require('../../assets/coins/dai.png'),
  wbtc: require('../../assets/coins/wbtc.png'),
  link: require('../../assets/coins/link.png'),
  uni: require('../../assets/coins/uni.png'),
  aave: require('../../assets/coins/aave.png'),
} satisfies Record<string, ImageSourcePropType>;

/** Native coin of each chain (Base and Arbitrum pay fees in ETH). */
const NATIVE: Record<ChainId, ImageSourcePropType> = {
  btc: L.btc,
  eth: L.eth,
  base: L.eth,
  arbitrum: L.eth,
  bnb: L.bnb,
  polygon: L.matic,
  sol: L.sol,
};

/** Small chain marker. Base and Arbitrum have no CC0 logo: letter badge instead. */
const CHAIN: Partial<Record<ChainId, ImageSourcePropType>> = {
  btc: L.btc,
  eth: L.eth,
  bnb: L.bnb,
  polygon: L.matic,
  sol: L.sol,
};

/* Issuer-published contract addresses, keyed `${chain}:${address}`. EVM addresses
   lower-case; Solana mints as-is. Mainnet first, then the official Circle testnet USDC. */
const TOKENS: Record<string, ImageSourcePropType> = {
  // Ethereum
  'eth:0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48': L.usdc,
  'eth:0xdac17f958d2ee523a2206206994597c13d831ec7': L.usdt,
  'eth:0x6b175474e89094c44da98b954eedeac495271d0f': L.dai,
  'eth:0x2260fac5e5542a773aa44fbcfedf7c193bc2c599': L.wbtc,
  'eth:0x514910771af9ca656af840dff83e8264ecf986ca': L.link,
  'eth:0x1f9840a85d5af5bf1d1762f925bdaddc4201f984': L.uni,
  'eth:0x7fc66500c84a76ad7e9c93437bfc5ac33e2ddae9': L.aave,
  // Base, Arbitrum, Polygon, BNB Chain
  'base:0x833589fcd6edb6e08f4c7c32d4f71b54bda02913': L.usdc,
  'arbitrum:0xaf88d065e77c8cc2239327c5edb3a432268e5831': L.usdc,
  'arbitrum:0xfd086bc7cd5c481dcc9c85ee8478a1c0b69fcbb9': L.usdt,
  'polygon:0x3c499c542cef5e3811e1192ce70d8cc03d5c3359': L.usdc,
  'polygon:0xc2132d05d31c914a87c6611c10748aeb04b58e8f': L.usdt,
  'bnb:0x8ac76a51cc950d9822d68b83fe1ad97b32cd580d': L.usdc,
  'bnb:0x55d398326f99059ff775485246999027b3197955': L.usdt,
  // Solana
  'sol:EPjFWdd5AufqSSqeM2qN1xzybapC8G4wEGGkZwyTDt1v': L.usdc,
  'sol:Es9vMFrzaCERmJfrF4H2FYD4KCoNkY11McCe8BenwNYB': L.usdt,
  // Testnets: Circle's USDC (Sepolia, Base Sepolia, Arbitrum Sepolia, Polygon Amoy, Solana devnet)
  'eth:0x1c7d4b196cb0c7b01d743fbc6116a902379c7238': L.usdc,
  'base:0x036cbd53842c5426634e7929541ec2318f3dcf7e': L.usdc,
  'arbitrum:0x75faf114eafb1bdbe2f0316df893fd58ce46aa4d': L.usdc,
  'polygon:0x41e94eb019c0762f9bfcf9fb1e58725bfb0e7582': L.usdc,
  'sol:4zMMC9srt5Ri5X14GAgXhaHii3GnPAEERYPJgZJDncDU': L.usdc,
  // Chainlink's own Sepolia faucet LINK (docs.chain.link, "LINK token contracts")
  'eth:0x779877a7b0d9e8603169ddbd7836e478b4624789': L.link,
  // NOT listed on purpose: the many other Sepolia "USDT" / "USDC" / "DAI" tokens. Tether and Maker
  // publish no official Sepolia contracts; on this test wallet alone there are ten tokens called
  // USDT or USDC, from ten different deployers.
};

/** Logo for a holding, or undefined (caller shows the letter badge). */
export function assetLogo(h: Pick<Holding, 'chain' | 'asset'>): ImageSourcePropType | undefined {
  if (h.asset.kind === 'native') return NATIVE[h.chain];
  const addr = h.chain === 'sol' ? h.asset.address : h.asset.address.toLowerCase();
  return TOKENS[`${h.chain}:${addr}`];
}

export function chainLogo(chain: ChainId): ImageSourcePropType | undefined {
  return CHAIN[chain];
}
