import { router } from 'expo-router';
import { useEffect, useRef, useState } from 'react';

import { useWallet } from '../../state/WalletContext';
import { requestBlePermissions, scanForWallets, waitForBluetooth, type FoundDevice } from '../../transport/ble';
import { Badge, Button, Card, Row, Screen, T, useTheme } from '../../ui/components';

export default function DeviceScreen() {
  const w = useWallet();
  const t = useTheme();
  const [found, setFound] = useState<FoundDevice[]>([]);
  const [scanning, setScanning] = useState(false);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const stop = useRef<(() => void) | null>(null);

  useEffect(() => () => stop.current?.(), []);

  const scan = async () => {
    setErr(null);
    setFound([]);
    try {
      if (!(await requestBlePermissions())) throw new Error('Bluetooth permission denied');
      setScanning(true);
      await waitForBluetooth();
      stop.current = scanForWallets(
        (d) => setFound((cur) => (cur.some((c) => c.id === d.id) ? cur : [...cur, d])),
        (e) => {
          setErr(e.message);
          setScanning(false);
        },
      );
      setTimeout(() => {
        stop.current?.();
        setScanning(false);
      }, 15_000);
    } catch (e) {
      setErr((e as Error).message);
      setScanning(false);
    }
  };

  const run = async (f: () => Promise<void>) => {
    setBusy(true);
    setErr(null);
    try {
      await f();
    } catch (e) {
      setErr((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  return (
    <Screen>
      <T kind="title">Device</T>
      {w.device ? (
        <Card>
          <Badge label={w.device.kind === 'mock' ? 'MOCK DEVICE' : w.device.kind === 'sim' ? 'SIMULATOR' : 'CONNECTED'} color={w.device.kind === 'ble' ? t.ok : t.warn} />
          <Row left="Name" right={w.device.name} />
          <Row left="Firmware" right={w.device.info.firmware} />
          <Row left="Protocol" right={String(w.device.info.protocol)} />
          <Row left="Locked" right={w.device.info.locked ? 'yes' : 'no'} />
          {w.device.info.locked || !w.device.info.initialized ? (
            <>
              <T kind="warn">{w.device.info.initialized ? 'Unlock the device with your PIN, then tap Reload.' : 'Set up the device first (create or import a wallet), then tap Reload.'}</T>
              <Button title="Reload" onPress={() => run(w.reloadDevice)} loading={busy} />
            </>
          ) : null}
          {w.device.kind === 'sim' ? <T kind="muted">Desktop simulator over ws://localhost:8765 (development builds only).</T> : null}
          {w.device.kind === 'mock' ? (
            <T kind="warn">Mock device: shows the PUBLIC test wallet “abandon … about”. It has no keys and cannot sign. Never send funds to it.</T>
          ) : null}
          <Button kind="secondary" title="Lock & disconnect" onPress={() => run(async () => {
            await w.device?.client.request({ type: 'lock' }).catch(() => undefined);
            await w.disconnect();
          })} />
        </Card>
      ) : (
        <Card>
          <T kind="h2">Pair your wallet</T>
          <T kind="muted">
            Turn the device on and choose Pair on it. Your phone and the device will both show a 6-digit code. Only accept if they match. Only one phone can be paired at a time.
          </T>
          <Button title={scanning ? 'Scanning…' : 'Scan for device'} onPress={scan} loading={scanning} />
          {found.map((d) => (
            <Row key={d.id} left={d.name} sub={d.id} right={d.rssi ? `${d.rssi} dBm` : undefined} onPress={() => run(() => w.connectBle(d.id))} />
          ))}
          <T kind="muted">
            Note: the encrypted app channel arrives with firmware milestone M5. Until then the app will not exchange data with real hardware. Use the mock device to try everything.
          </T>
          {__DEV__ ? <Button title="Connect to tredder simulator (dev)" onPress={() => run(() => w.connectSim())} loading={busy} /> : null}
          <Button kind="secondary" title="Use mock device (development)" onPress={() => run(w.connectMock)} loading={busy} />
        </Card>
      )}
      {err ? <T kind="error">{err}</T> : null}
      <Card>
        <Button kind="secondary" title="Find accounts from an imported seed" onPress={() => router.push('/import')} />
        <Button kind="secondary" title="Sweep imported seed to new wallet" onPress={() => router.push('/sweep')} />
        <Button kind="secondary" title="Settings" onPress={() => router.push('/settings')} />
      </Card>
    </Screen>
  );
}
