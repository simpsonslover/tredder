import { bytesToHex } from '@noble/hashes/utils.js';
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';

import type { TxRecord } from '../chains/types';
import { DeviceClient } from '../protocol/client';
import { pairCode as codeOf } from '../protocol/noise';
import { handshake } from '../protocol/secure';
import type { DeviceInfo, Net } from '../protocol/types';
import { BleTransport } from '../transport/ble';
import { MockTransport } from '../transport/mock';
import { DEFAULT_SIM_URL, SimTransport } from '../transport/sim';
import { IdentityChangedError, appStaticKey, pinDeviceKey, pinnedDeviceKey } from './pairing';
import { loadJson, saveJson, secureKV } from './storage';
import { DEFAULT_SCHEMES, accountId, fetchAccounts, loadHistory, loadPortfolio, type Portfolio, type WalletAccount } from './wallet';

export interface Settings {
  net: Net;
  mainnetEnabled: boolean; // spec: testnets first; mainnet only after explicit opt-in
  dustUsd: number;
}
const DEFAULT_SETTINGS: Settings = { net: 'test', mainnetEnabled: false, dustUsd: 1 };

interface Device {
  client: DeviceClient;
  info: DeviceInfo;
  kind: 'ble' | 'mock' | 'sim';
  name: string;
}

interface Ctx {
  ready: boolean;
  settings: Settings;
  updateSettings(p: Partial<Settings>): Promise<void>;
  device: Device | null;
  awaitingUser: boolean;
  /** What the device is waiting for: 'pair' (pairing code) or the request type, e.g. 'show-address'. */
  awaitingKind: string | null;
  /** BLE: the 6-digit code of this secure session (the device shows the same one when it asks to pair). */
  pairCode: string | null;
  connectMock(): Promise<void>;
  /** Throws IdentityChangedError if the device key is not the pinned one, unless acceptNewIdentity. */
  connectBle(id: string, opts?: { acceptNewIdentity?: boolean }): Promise<void>;
  connectSim(url?: string): Promise<void>;
  /** Re-read device state (e.g. after unlocking it) and load default accounts. */
  reloadDevice(): Promise<void>;
  disconnect(): Promise<void>;
  accounts: WalletAccount[];
  setAccounts(a: WalletAccount[]): Promise<void>;
  portfolio: Portfolio | null;
  history: TxRecord[];
  loading: boolean;
  error: string | null;
  refresh(): Promise<void>;
}

const WalletCtx = createContext<Ctx | null>(null);

export function WalletProvider({ children }: { children: ReactNode }) {
  const [ready, setReady] = useState(false);
  const [settings, setSettings] = useState<Settings>(DEFAULT_SETTINGS);
  const [device, setDevice] = useState<Device | null>(null);
  const [awaitingUser, setAwaiting] = useState(false);
  const [awaitingKind, setAwaitingKind] = useState<string | null>(null);
  const [pairCode, setPairCode] = useState<string | null>(null);
  const currentReq = useRef<string | null>(null);
  const [byNet, setByNet] = useState<Record<Net, WalletAccount[]>>({ main: [], test: [] });
  const [portfolio, setPortfolio] = useState<Portfolio | null>(null);
  const [history, setHistory] = useState<TxRecord[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const deviceRef = useRef<Device | null>(null);

  useEffect(() => {
    (async () => {
      const s = await loadJson<Settings>(secureKV, 'settings');
      if (s) setSettings({ ...DEFAULT_SETTINGS, ...s, net: s.mainnetEnabled ? s.net : 'test' });
      const a = await loadJson<Record<Net, WalletAccount[]>>(secureKV, 'accounts');
      if (a) setByNet(a);
      setReady(true);
    })();
  }, []);

  const updateSettings = useCallback(async (p: Partial<Settings>) => {
    setSettings((cur) => {
      const next = { ...cur, ...p };
      if (!next.mainnetEnabled) next.net = 'test';
      void saveJson(secureKV, 'settings', next);
      return next;
    });
    setPortfolio(null);
    setHistory([]);
  }, []);

  const accounts = byNet[settings.net];
  const setAccounts = useCallback(
    async (a: WalletAccount[]) => {
      const uniq = [...new Map(a.map((x) => [accountId(x), x])).values()];
      const next = { ...byNet, [settings.net]: uniq };
      setByNet(next);
      await saveJson(secureKV, 'accounts', next);
    },
    [byNet, settings.net],
  );

  const attach = useCallback(
    async (client: DeviceClient, kind: Device['kind']) => {
      const info = await client.request({ type: 'get-info' });
      const d = { client, info, kind, name: client.transportName };
      deviceRef.current = d;
      setDevice(d);
      if (!accounts.length && info.initialized && !info.locked) {
        const fetched = await fetchAccounts(client, settings.net, DEFAULT_SCHEMES, [0]);
        await setAccounts(fetched);
      }
    },
    [accounts.length, settings.net, setAccounts],
  );

  const onEvent = useCallback((e: { event: string; detail?: string }) => {
    if (e.event === 'awaiting-user') {
      setAwaiting(true);
      setAwaitingKind(e.detail === 'pair' ? 'pair' : currentReq.current);
    }
    if (e.event === 'locked') setError('Device locked');
  }, []);

  const wrapClient = useCallback(
    (c: DeviceClient) => {
      // Clear the "confirm on device" overlay whenever a request settles.
      const orig = c.request.bind(c);
      c.request = (async (req: Parameters<typeof orig>[0]) => {
        currentReq.current = req.type;
        try {
          return await orig(req);
        } finally {
          currentReq.current = null;
          setAwaiting(false);
          setAwaitingKind(null);
        }
      }) as typeof c.request;
      return c;
    },
    [],
  );

  const disconnect = useCallback(async () => {
    const d = deviceRef.current;
    deviceRef.current = null;
    setDevice(null);
    setAwaiting(false);
    setPairCode(null);
    await d?.client.close().catch(() => undefined);
  }, []);

  const connectMock = useCallback(async () => {
    await disconnect();
    setError(null);
    const t = new MockTransport();
    t.onDisconnect(() => void disconnect());
    await attach(wrapClient(new DeviceClient(t, onEvent)), 'mock');
  }, [attach, disconnect, onEvent, wrapClient]);

  const connectBle = useCallback(
    async (id: string, opts: { acceptNewIdentity?: boolean } = {}) => {
      await disconnect();
      setError(null);
      const t = await BleTransport.connect(id);
      try {
        const session = await handshake(t, await appStaticKey());
        const pinned = await pinnedDeviceKey(id);
        const remote = bytesToHex(session.remoteStatic);
        if (pinned && pinned !== remote && !opts.acceptNewIdentity) throw new IdentityChangedError();
        t.onDisconnect(() => void disconnect()); // auto-lock: device locks on disconnect, app forgets the session
        setPairCode(codeOf(session.hash));
        const client = wrapClient(new DeviceClient(t, onEvent, session));
        // Pin the device key once the device has accepted us (it answers nothing but get-info to an
        // unpaired app), so a later impostor with another key is caught before any request.
        if (pinned !== remote) {
          const orig = client.request.bind(client);
          client.request = (async (req: Parameters<typeof orig>[0]) => {
            const r = await orig(req);
            if (req.type !== 'get-info') await pinDeviceKey(id, session.remoteStatic);
            return r;
          }) as typeof client.request;
        }
        await attach(client, 'ble');
      } catch (e) {
        await t.close();
        throw e;
      }
    },
    [attach, disconnect, onEvent, wrapClient],
  );

  const connectSim = useCallback(
    async (url: string = DEFAULT_SIM_URL) => {
      await disconnect();
      setError(null);
      const t = await SimTransport.connect(url);
      t.onDisconnect(() => void disconnect());
      try {
        await attach(wrapClient(new DeviceClient(t, onEvent)), 'sim');
      } catch (e) {
        await t.close();
        throw e;
      }
    },
    [attach, disconnect, onEvent, wrapClient],
  );

  const reloadDevice = useCallback(async () => {
    const d = deviceRef.current;
    if (!d) return;
    setError(null);
    await attach(d.client, d.kind);
  }, [attach]);

  const refresh = useCallback(async () => {
    if (!accounts.length) return;
    setLoading(true);
    setError(null);
    try {
      const p = await loadPortfolio(accounts, settings.net);
      setPortfolio(p);
      setHistory(await loadHistory(accounts, settings.net, p));
    } catch (e) {
      setError((e as Error).message);
    } finally {
      setLoading(false);
    }
  }, [accounts, settings.net]);

  // Load balances once accounts are known (deferred: not during render/effect body).
  useEffect(() => {
    if (!ready || !accounts.length || portfolio || loading) return;
    const id = setTimeout(() => void refresh(), 0);
    return () => clearTimeout(id);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [ready, accounts, settings.net]);

  const value = useMemo<Ctx>(
    () => ({ ready, settings, updateSettings, device, awaitingUser, awaitingKind, pairCode, connectMock, connectBle, connectSim, reloadDevice, disconnect, accounts, setAccounts, portfolio, history, loading, error, refresh }),
    [ready, settings, updateSettings, device, awaitingUser, awaitingKind, pairCode, connectMock, connectBle, connectSim, reloadDevice, disconnect, accounts, setAccounts, portfolio, history, loading, error, refresh],
  );
  return <WalletCtx.Provider value={value}>{children}</WalletCtx.Provider>;
}

export function useWallet(): Ctx {
  const c = useContext(WalletCtx);
  if (!c) throw new Error('useWallet outside WalletProvider');
  return c;
}
