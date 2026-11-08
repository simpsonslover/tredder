// Real BLE transport (react-native-ble-plx). Needs a development build.
//
// Frames only: no BLE-level bonding. Security comes from the Noise channel on top
// (protocol §3, protocol/secure.ts): DeviceClient refuses a BLE transport without a
// Noise session, and the pairing code shown on both screens comes from the handshake.
import { base64 } from '@scure/base';
import { PermissionsAndroid, Platform } from 'react-native';
import { BleManager, State, type Device, type Subscription } from 'react-native-ble-plx';

import type { Transport } from './types';

export const SERVICE_UUID = '7a3f0001-5c1e-4c6b-9f2e-6b7d2a1e0b10';
export const RX_UUID = '7a3f0002-5c1e-4c6b-9f2e-6b7d2a1e0b10'; // app -> device
export const TX_UUID = '7a3f0003-5c1e-4c6b-9f2e-6b7d2a1e0b10'; // device -> app
const WANT_MTU = 247;

let manager: BleManager | null = null;
const ble = () => (manager ??= new BleManager());

const STATE_HELP: Partial<Record<State, string>> = {
  [State.PoweredOff]: 'Bluetooth is off. Turn it on in Control Center or Settings, then scan again.',
  [State.Unauthorized]:
    'Bluetooth permission is off for Tredder Wallet. Allow it in Settings → Privacy & Security → Bluetooth (Android: app permissions → Nearby devices), then scan again.',
  [State.Unsupported]: 'This phone does not support Bluetooth Low Energy.',
};

/**
 * Wait until the Bluetooth stack reports PoweredOn. Right after the BleManager
 * is created (and while iOS shows its permission prompt) the state is
 * "Unknown" or "Resetting"; scanning then fails with "BluetoothLE is in
 * unknown state", so never scan before this resolves.
 */
export function waitForBluetooth(timeoutMs = 10_000): Promise<void> {
  return new Promise((resolve, reject) => {
    let sub: Subscription | null = null;
    let done = false;
    const finish = (err?: Error) => {
      if (done) return;
      done = true;
      clearTimeout(timer);
      sub?.remove();
      if (err) reject(err);
      else resolve();
    };
    const timer = setTimeout(
      () => finish(new Error('Bluetooth did not become ready. Check that it is on and that Tredder Wallet may use it, then scan again.')),
      timeoutMs,
    );
    sub = ble().onStateChange((state) => {
      if (state === State.PoweredOn) finish();
      else if (STATE_HELP[state]) finish(new Error(STATE_HELP[state]));
      // Unknown / Resetting: keep waiting (iOS permission prompt, radio starting)
    }, true);
    if (done) sub.remove();
  });
}

export async function requestBlePermissions(): Promise<boolean> {
  if (Platform.OS !== 'android') return true;
  if (Platform.Version >= 31) {
    const r = await PermissionsAndroid.requestMultiple([
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_SCAN,
      PermissionsAndroid.PERMISSIONS.BLUETOOTH_CONNECT,
    ]);
    return Object.values(r).every((v) => v === PermissionsAndroid.RESULTS.GRANTED);
  }
  const r = await PermissionsAndroid.request(PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION);
  return r === PermissionsAndroid.RESULTS.GRANTED;
}

export interface FoundDevice {
  id: string;
  name: string;
  rssi: number | null;
}

/** Scan for wallets advertising our service UUID. Returns a stop function. */
export function scanForWallets(onFound: (d: FoundDevice) => void, onError: (e: Error) => void): () => void {
  ble().startDeviceScan([SERVICE_UUID], { allowDuplicates: false }, (error, dev) => {
    if (error) onError(new Error(error.message));
    else if (dev) onFound({ id: dev.id, name: dev.name ?? dev.localName ?? 'Wallet', rssi: dev.rssi });
  });
  return () => void ble().stopDeviceScan();
}

export class BleTransport implements Transport {
  readonly kind = 'ble' as const;
  private frameCbs = new Set<(f: Uint8Array) => void>();
  private discCbs = new Set<() => void>();
  private subs: Subscription[] = [];

  private constructor(
    private readonly device: Device,
    readonly frameSize: number,
  ) {}

  get name() {
    return this.device.name ?? 'Wallet';
  }

  static async connect(id: string): Promise<BleTransport> {
    await waitForBluetooth();
    const m = ble();
    await m.stopDeviceScan();
    let dev = await m.connectToDevice(id, { requestMTU: WANT_MTU, timeout: 15_000 });
    dev = await m.discoverAllServicesAndCharacteristicsForDevice(dev.id);
    const t = new BleTransport(dev, Math.max(20, (dev.mtu || 23) - 3));
    t.subs.push(
      m.monitorCharacteristicForDevice(dev.id, SERVICE_UUID, TX_UUID, (err, c) => {
        if (err || !c?.value) return;
        const frame = base64.decode(c.value);
        t.frameCbs.forEach((cb) => cb(frame));
      }),
      m.onDeviceDisconnected(dev.id, () => t.discCbs.forEach((cb) => cb())),
    );
    return t;
  }

  async send(frame: Uint8Array): Promise<void> {
    await ble().writeCharacteristicWithResponseForDevice(this.device.id, SERVICE_UUID, RX_UUID, base64.encode(frame));
  }

  onFrame(cb: (f: Uint8Array) => void) {
    this.frameCbs.add(cb);
    return () => this.frameCbs.delete(cb);
  }

  onDisconnect(cb: () => void) {
    this.discCbs.add(cb);
    return () => this.discCbs.delete(cb);
  }

  async close() {
    this.subs.forEach((s) => s.remove());
    this.subs = [];
    await ble().cancelDeviceConnection(this.device.id).catch(() => undefined);
  }
}
