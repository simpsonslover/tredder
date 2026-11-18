// Chain registry. Add a chain = add an entry here (+ a family service if new).
import type { Net, Scheme } from '../protocol/types';

export type Family = 'btc' | 'evm' | 'sol';
export type ChainId = 'btc' | 'eth' | 'base' | 'bnb' | 'polygon' | 'arbitrum' | 'sol';

export interface ChainConfig {
  id: ChainId;
  family: Family;
  name: string;
  symbol: string; // native coin
  decimals: number;
  schemes: Scheme[]; // derivation schemes that produce addresses on this chain
  net: Record<
    Net,
    {
      label: string;
      evmChainId?: number;
      rpc?: string;
      /** EVM: more RPCs tried in order when the first fails (all checked: same chain id). */
      rpcFallbacks?: string[];
      esplora?: string; // BTC REST API
      /** BTC: more Esplora servers tried in order when the first fails (same data, other operator). */
      esploraFallbacks?: string[];
      blockscout?: string; // EVM token balances + history (no API key)
      explorerTx: string; // prefix, tx hash appended
      explorerAddr: string;
    }
  >;
}

// 2026-11-16: polygon-rpc.com answers every call with 401 "API key disabled", and
// polygon.blockscout.com sits behind a Cloudflare bot check: Polygon shows POL only (no token list).
// mempool.space failed TLS on some networks: Blockstream's Esplora (same API) is the fallback.
const EVM_SCHEMES: Scheme[] = ['evm-bip44', 'evm-ledger-live'];

export const CHAINS: Record<ChainId, ChainConfig> = {
  btc: {
    id: 'btc', family: 'btc', name: 'Bitcoin', symbol: 'BTC', decimals: 8,
    schemes: ['btc-p2wpkh', 'btc-p2tr', 'btc-p2sh-p2wpkh', 'btc-p2pkh'],
    net: {
      main: { label: 'Bitcoin', esplora: 'https://mempool.space/api', esploraFallbacks: ['https://blockstream.info/api'], explorerTx: 'https://mempool.space/tx/', explorerAddr: 'https://mempool.space/address/' },
      test: { label: 'Bitcoin testnet4', esplora: 'https://mempool.space/testnet4/api', esploraFallbacks: ['https://blockstream.info/testnet4/api'], explorerTx: 'https://mempool.space/testnet4/tx/', explorerAddr: 'https://mempool.space/testnet4/address/' },
    },
  },
  eth: {
    id: 'eth', family: 'evm', name: 'Ethereum', symbol: 'ETH', decimals: 18,
    schemes: EVM_SCHEMES,
    net: {
      main: { label: 'Ethereum', evmChainId: 1, rpc: 'https://ethereum-rpc.publicnode.com', rpcFallbacks: ['https://eth.drpc.org'], blockscout: 'https://eth.blockscout.com', explorerTx: 'https://etherscan.io/tx/', explorerAddr: 'https://etherscan.io/address/' },
      test: { label: 'Sepolia', evmChainId: 11155111, rpc: 'https://ethereum-sepolia-rpc.publicnode.com', blockscout: 'https://eth-sepolia.blockscout.com', explorerTx: 'https://sepolia.etherscan.io/tx/', explorerAddr: 'https://sepolia.etherscan.io/address/' },
    },
  },
  base: {
    id: 'base', family: 'evm', name: 'Base', symbol: 'ETH', decimals: 18,
    schemes: EVM_SCHEMES,
    net: {
      main: { label: 'Base', evmChainId: 8453, rpc: 'https://mainnet.base.org', rpcFallbacks: ['https://base-rpc.publicnode.com', 'https://base.drpc.org'], blockscout: 'https://base.blockscout.com', explorerTx: 'https://basescan.org/tx/', explorerAddr: 'https://basescan.org/address/' },
      test: { label: 'Base Sepolia', evmChainId: 84532, rpc: 'https://sepolia.base.org', rpcFallbacks: ['https://base-sepolia-rpc.publicnode.com', 'https://base-sepolia.drpc.org'], blockscout: 'https://base-sepolia.blockscout.com', explorerTx: 'https://sepolia.basescan.org/tx/', explorerAddr: 'https://sepolia.basescan.org/address/' },
    },
  },
  bnb: {
    id: 'bnb', family: 'evm', name: 'BNB Chain', symbol: 'BNB', decimals: 18,
    schemes: EVM_SCHEMES,
    net: {
      main: { label: 'BNB Chain', evmChainId: 56, rpc: 'https://bsc-dataseed.bnbchain.org', rpcFallbacks: ['https://bsc-dataseed1.defibit.io'], explorerTx: 'https://bscscan.com/tx/', explorerAddr: 'https://bscscan.com/address/' },
      test: { label: 'BNB testnet', evmChainId: 97, rpc: 'https://bsc-testnet-dataseed.bnbchain.org', explorerTx: 'https://testnet.bscscan.com/tx/', explorerAddr: 'https://testnet.bscscan.com/address/' },
    },
  },
  polygon: {
    id: 'polygon', family: 'evm', name: 'Polygon', symbol: 'POL', decimals: 18,
    schemes: EVM_SCHEMES,
    net: {
      main: { label: 'Polygon', evmChainId: 137, rpc: 'https://polygon-bor-rpc.publicnode.com', rpcFallbacks: ['https://polygon.drpc.org'], explorerTx: 'https://polygonscan.com/tx/', explorerAddr: 'https://polygonscan.com/address/' },
      test: { label: 'Polygon Amoy', evmChainId: 80002, rpc: 'https://polygon-amoy-bor-rpc.publicnode.com', rpcFallbacks: ['https://polygon-amoy.drpc.org'], explorerTx: 'https://amoy.polygonscan.com/tx/', explorerAddr: 'https://amoy.polygonscan.com/address/' },
    },
  },
  arbitrum: {
    id: 'arbitrum', family: 'evm', name: 'Arbitrum', symbol: 'ETH', decimals: 18,
    schemes: EVM_SCHEMES,
    net: {
      main: { label: 'Arbitrum One', evmChainId: 42161, rpc: 'https://arb1.arbitrum.io/rpc', rpcFallbacks: ['https://arbitrum-one-rpc.publicnode.com', 'https://arbitrum.drpc.org'], blockscout: 'https://arbitrum.blockscout.com', explorerTx: 'https://arbiscan.io/tx/', explorerAddr: 'https://arbiscan.io/address/' },
      test: { label: 'Arbitrum Sepolia', evmChainId: 421614, rpc: 'https://sepolia-rollup.arbitrum.io/rpc', rpcFallbacks: ['https://arbitrum-sepolia-rpc.publicnode.com', 'https://arbitrum-sepolia.drpc.org'], blockscout: 'https://arbitrum-sepolia.blockscout.com', explorerTx: 'https://sepolia.arbiscan.io/tx/', explorerAddr: 'https://sepolia.arbiscan.io/address/' },
    },
  },
  sol: {
    id: 'sol', family: 'sol', name: 'Solana', symbol: 'SOL', decimals: 9,
    schemes: ['sol-bip44-change', 'sol-bip44', 'sol-bip44-root', 'sol-cli'],
    net: {
      main: { label: 'Solana', rpc: 'https://api.mainnet-beta.solana.com', explorerTx: 'https://explorer.solana.com/tx/', explorerAddr: 'https://explorer.solana.com/address/' },
      test: { label: 'Solana devnet', rpc: 'https://api.devnet.solana.com', explorerTx: 'https://explorer.solana.com/tx/', explorerAddr: 'https://explorer.solana.com/address/' },
    },
  },
};

export const CHAIN_LIST = Object.values(CHAINS);

export function explorerTx(chain: ChainId, net: Net, hash: string) {
  const c = CHAINS[chain].net[net];
  return c.explorerTx + hash + (chain === 'sol' && net === 'test' ? '?cluster=devnet' : '');
}

export const SCHEME_LABEL: Record<Scheme, string> = {
  'btc-p2pkh': 'Legacy (BIP44)',
  'btc-p2sh-p2wpkh': 'Nested SegWit (BIP49)',
  'btc-p2wpkh': 'Native SegWit (BIP84)',
  'btc-p2tr': 'Taproot (BIP86)',
  'evm-bip44': 'MetaMask / standard',
  'evm-ledger-live': 'Ledger Live',
  'sol-bip44-change': 'Phantom / Solflare',
  'sol-bip44': 'Solana BIP44 (older wallets)',
  'sol-bip44-root': 'Solana root path',
  'sol-cli': 'Solana CLI seed',
};
