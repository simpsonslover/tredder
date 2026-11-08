import { router } from 'expo-router';
import { useEffect, useRef, useState } from 'react';
import { ActivityIndicator, Alert, Text, View } from 'react-native';

import { IdentityChangedError } from '../../state/pairing';
import { useWallet } from '../../state/WalletContext';
import { requestBlePermissions, scanForWallets, waitForBluetooth, type FoundDevice } from '../../transport/ble';
import { Button, KeyValue, ListGroup, ListItem, MONO, Notice, Pane, Row, Screen, StatusDot, T, ToolbarButton, useTheme } from '../../ui/components';

function StepNo({ n, done }: { n: number; done?: boolean }) {
  const t = useTheme();
  return (
    <View style={{ width: 32, height: 32, borderRadius: 16, borderWidth: 2, borderColor: done ? t.ok : t.accent, alignItems: 'center', justifyContent: 'center' }}>
      <Text style={{ color: done ? t.ok : t.accent, fontSize: 15, fontWeight: '700' }}>{done ? '✓' : n}</Text>
    </View>
  );
}

export default function DeviceScreen() {
  const w = useWallet();
  const t = useTheme();
  const [found, setFound] = useState<FoundDevice[]>([]);
  const [scanning, setScanning] = useState(false);
  const [busy, setBusy] = useState<string | null>(null);
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

  const run = async (what: string, f: () => Promise<void>) => {
    setBusy(what);
    setErr(null);
    try {
      await f();
    } catch (e) {
      setErr((e as Error).message);
    } finally {
      setBusy(null);
    }
  };

  // A changed device key: never continue silently. Pairing again means comparing the 6-digit
  // code on both screens, which a fake device cannot match.
  const connect = async (id: string) => {
    stop.current?.();
    setScanning(false);
    try {
      await w.connectBle(id);
    } catch (e) {
      if (!(e instanceof IdentityChangedError)) throw e;
      const again = await new Promise<boolean>((resolve) =>
        Alert.alert('Device identity changed', `${e.message}\n\nContinue only if you restarted or reset this device yourself. You will compare a pairing code on both screens.`, [
          { text: 'Cancel', style: 'cancel', onPress: () => resolve(false) },
          { text: 'Pair again', style: 'destructive', onPress: () => resolve(true) },
        ]),
      );
      if (again) await w.connectBle(id, { acceptNewIdentity: true });
    }
  };

  const d = w.device;
  const state = !d ? 'Not connected' : !d.info.initialized ? 'Not set up' : d.info.locked ? 'Locked' : 'Unlocked';
  const link = !d ? '—' : d.kind === 'ble' ? 'Bluetooth, encrypted (Noise)' : d.kind === 'sim' ? 'Simulator (plaintext)' : 'In-app mock';

  return (
    <Screen title="Device" icon="hardware-chip" right={<ToolbarButton icon="settings-outline" label="Preferences" onPress={() => router.push('/settings')} />}>
      <Pane style={{ paddingHorizontal: 18, gap: 8 }}>
        <View style={{ flexDirection: 'row', alignItems: 'center', justifyContent: 'space-between' }}>
          <Text numberOfLines={1} style={{ color: t.text, fontSize: 20, flex: 1 }}>{d ? d.name : 'tredder'}</Text>
          <StatusDot color={!d ? t.muted : d.kind === 'ble' ? t.ok : t.warn} label={!d ? 'Offline' : d.kind === 'ble' ? 'Connected' : d.kind === 'sim' ? 'Simulator' : 'Mock'} />
        </View>
        <KeyValue label="Status" value={state} mono={false} />
        <KeyValue label="Firmware" value={d ? d.info.firmware : '—'} />
        <KeyValue label="Link" value={link} mono={false} />
      </Pane>

      {d && (d.info.locked || !d.info.initialized) ? (
        <Notice icon="lock-closed">{d.info.initialized ? 'Unlock the device with your PIN: the app updates by itself (or tap Reload).' : 'Set up the device first (create a wallet on it), then tap Reload.'}</Notice>
      ) : null}
      {d?.kind === 'mock' ? <Notice icon="flask">Mock device: the PUBLIC test wallet “abandon … about”. No keys, cannot sign. Never send funds to it.</Notice> : null}
      {err ? <Notice kind="error">{err}</Notice> : null}

      {d ? (
        <View style={{ gap: 10 }}>
          {d.info.locked || !d.info.initialized ? <Button title="Reload" icon="refresh" onPress={() => run('reload', w.reloadDevice)} loading={busy === 'reload'} /> : null}
          <Button
            kind="secondary"
            icon="power"
            title="Lock & disconnect"
            loading={busy === 'lock'}
            onPress={() =>
              run('lock', async () => {
                await w.device?.client.request({ type: 'lock' }).catch(() => undefined);
                await w.disconnect();
              })
            }
          />
        </View>
      ) : (
        <>
          <ListGroup title="Pair">
            <Row icon={<StepNo n={1} />} left="Unlock your tredder" sub="Turn it on and enter your PIN." />
            <Row icon={<StepNo n={2} done={found.length > 0} />} left="Scan and tap it" sub="Bluetooth on, phone close to the device." />
            <Row icon={<StepNo n={3} />} left="Compare the code" sub="First time only: approve on the device if both 6-digit codes match." />
          </ListGroup>
          <Button title={scanning ? 'Scanning…' : 'Scan for device'} icon="bluetooth" onPress={scan} loading={scanning} />
          {found.length ? (
            <ListGroup title="Found">
              {found.map((f) => (
                <ListItem
                  key={f.id}
                  icon="hardware-chip"
                  color={t.accent}
                  title={f.name}
                  sub={f.rssi ? `signal ${f.rssi} dBm` : f.id}
                  value={busy === f.id ? <ActivityIndicator color={t.accent} /> : undefined}
                  onPress={() => run(f.id, () => connect(f.id))}
                />
              ))}
            </ListGroup>
          ) : null}
          <ListGroup title="Developer" footer="Test without hardware. The mock device holds no keys and cannot sign.">
            <ListItem icon="flask-outline" title="Use mock device" onPress={() => run('mock', w.connectMock)} value={busy === 'mock' ? <ActivityIndicator color={t.accent} /> : undefined} />
            {__DEV__ ? <ListItem icon="desktop-outline" title="Connect to simulator" sub="ws://localhost:8765" onPress={() => run('sim', () => w.connectSim())} /> : null}
          </ListGroup>
        </>
      )}
      <T kind="muted" center>
        <Text style={{ fontFamily: MONO }}>tredder</Text> · keys never leave the device
      </T>
    </Screen>
  );
}
