import type { Net, Scheme } from '../protocol/types';
import type { ChainId } from './config';

export type AssetRef =
  | { kind: 'native' }
  | { kind: 'token'; address: string; standard: 'erc20' | 'spl' | 'spl-2022' };

export type HoldingKind = 'native' | 'token' | 'nft' | 'staked' | 'lp';

export interface Holding {
  chain: ChainId;
  net: Net;
  scheme: Scheme;
  account: number;
  owner: string; // address holding it (BTC: account label)
  asset: AssetRef;
  kind: HoldingKind;
  symbol: string;
  name: string;
  decimals: number;
  amount: bigint; // base units
  usdPrice?: number;
  usdValue?: number;
}

export interface TxRecord {
  chain: ChainId;
  hash: string;
  time?: number; // unix seconds
  direction: 'in' | 'out' | 'self';
  amount?: string; // human readable incl. symbol
  status: 'pending' | 'confirmed' | 'failed';
}

export class SimulationError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'SimulationError';
  }
}

export const assetKey = (h: Pick<Holding, 'chain' | 'asset'>) =>
  `${h.chain}:${h.asset.kind === 'native' ? 'native' : h.asset.address.toLowerCase()}`;
