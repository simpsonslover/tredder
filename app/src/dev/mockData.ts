// Typed view of mockDeviceData.json (exported by firmware/tools/mock_export.c).
import raw from './mockDeviceData.json';

import type { Net, Scheme } from '../protocol/types';

type Base = { net: Net; scheme: Scheme; account: number; path: string };
export type MockAccount =
  | (Base & { kind: 'xpub'; publicKey: string; chainCode: string; masterFingerprint: number })
  | (Base & { kind: 'address'; address: string });
export interface MockBtcAddress {
  net: Net;
  scheme: Scheme;
  account: number;
  change: number;
  index: number;
  address: string;
}

export const MOCK = raw as unknown as { note: string; accounts: MockAccount[]; btcAddresses: MockBtcAddress[] };
