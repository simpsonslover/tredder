// Message types for protocol v1. Spec: protocol/PROTOCOL.md (repo root).
export const PROTOCOL_VERSION = 1;

export const SCHEMES = [
  'btc-p2pkh',
  'btc-p2sh-p2wpkh',
  'btc-p2wpkh',
  'btc-p2tr',
  'evm-bip44',
  'evm-ledger-live',
  'sol-bip44-change',
  'sol-bip44',
  'sol-bip44-root',
  'sol-cli',
] as const;
export type Scheme = (typeof SCHEMES)[number];
export type Net = 'main' | 'test';
export type WalletRef = 'active' | 'sweep-source';

export type AccountKey =
  | {
      account: number;
      path: string;
      kind: 'xpub';
      publicKey: Uint8Array; // 33-byte compressed
      chainCode: Uint8Array; // 32 bytes
      masterFingerprint: number;
    }
  | { account: number; path: string; kind: 'address'; address: string };

export interface EvmHints {
  tokenSymbol?: string;
  tokenDecimals?: number;
}

export interface SweepPlanSummary {
  id: string;
  steps: {
    chain: string;
    asset: string;
    amount: string; // decimal string, for display only
    to: string;
    feeEstimate: string;
  }[];
}

export type Request =
  | { type: 'get-info' }
  | { type: 'get-accounts'; wallet: WalletRef; net: Net; scheme: Scheme; accounts: number[] }
  | { type: 'show-address'; wallet: WalletRef; net: Net; scheme: Scheme; account: number; change: number; index: number }
  // BTC: which keys sign is in the PSBT (bip32Derivation / tapBip32Derivation).
  | { type: 'sign-btc'; wallet: WalletRef; net: Net; psbt: Uint8Array; planStep?: number }
  // EVM / SOL: the key is named explicitly (scheme + account). The device derives
  // the address itself and checks it is the transaction's sender.
  | { type: 'sign-evm'; wallet: WalletRef; scheme: Scheme; account: number; chainId: number; tx: Uint8Array; hints?: EvmHints; planStep?: number }
  | { type: 'sign-sol'; wallet: WalletRef; scheme: Scheme; account: number; net: Net; message: Uint8Array; hints?: { tokenSymbol?: string }; planStep?: number }
  | { type: 'sweep-plan'; plan: SweepPlanSummary }
  | { type: 'forget-sweep-source' }
  | { type: 'lock' };

export interface DeviceInfo {
  model: string;
  firmware: string;
  protocol: number;
  initialized: boolean;
  locked: boolean;
  hasSweepSource: boolean;
}

// Result type for each request type.
export interface ResultMap {
  'get-info': DeviceInfo;
  'get-accounts': { accounts: AccountKey[] };
  'show-address': { address: string; confirmed: boolean };
  'sign-btc': { psbt: Uint8Array };
  'sign-evm': { r: `0x${string}`; s: `0x${string}`; yParity: 0 | 1 };
  'sign-sol': { signature: Uint8Array; publicKey: string };
  'sweep-plan': { approved: boolean; planHash: string };
  'forget-sweep-source': { erased: boolean };
  lock: Record<string, never>;
}

export const ERROR_CODES = [
  'USER_REJECTED',
  'LOCKED',
  'BUSY',
  'UNSUPPORTED',
  'INVALID_REQUEST',
  'NOT_INITIALIZED',
  'TIMEOUT',
  'INTERNAL',
] as const;
export type ErrorCode = (typeof ERROR_CODES)[number];

export type WireRequest = Request & { id: number };
export type WireResponse =
  | { id: number; ok: true; result: unknown }
  | { id: number; ok: false; error: { code: ErrorCode; message: string } };
export type WireEvent = {
  type: 'event';
  event: 'awaiting-user' | 'locked' | 'unlocked' | 'paired' | 'pair-rejected' | 'progress';
  id?: number;
  detail?: string;
};

export class DeviceError extends Error {
  constructor(
    public readonly code: ErrorCode,
    message: string,
  ) {
    super(message);
    this.name = 'DeviceError';
  }
}

// Requests that make the device wait for a button press get a long timeout.
export const NEEDS_USER: ReadonlySet<Request['type']> = new Set(['show-address', 'sign-btc', 'sign-evm', 'sign-sol', 'sweep-plan', 'forget-sweep-source']);
