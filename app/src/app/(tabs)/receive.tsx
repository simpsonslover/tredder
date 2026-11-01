import * as Clipboard from 'expo-clipboard';
import { useState } from 'react';
import { View } from 'react-native';
import QRCode from 'react-native-qrcode-svg';

import { CHAINS, SCHEME_LABEL } from '../../chains/config';
import { accountId, verifyReceiveAddress } from '../../state/wallet';
import { useWallet } from '../../state/WalletContext';
import { Button, Card, Choice, Screen, T } from '../../ui/components';

export default function Receive() {
  const w = useWallet();
  const [sel, setSel] = useState<string | undefined>(w.accounts[0] && accountId(w.accounts[0]));
  const [verified, setVerified] = useState<{ id: string; address: string; index: number } | null>(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const acct = w.accounts.find((a) => accountId(a) === sel);

  const verify = async () => {
    if (!acct || !w.device) return;
    setBusy(true);
    setErr(null);
    setVerified(null);
    try {
      const r = await verifyReceiveAddress(w.device.client, acct, w.settings.net, w.portfolio);
      setVerified({ id: accountId(acct), ...r });
    } catch (e) {
      setErr((e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  const chainsFor = (scheme: string) =>
    scheme.startsWith('btc') ? CHAINS.btc.net[w.settings.net].label : scheme.startsWith('evm') ? 'Ethereum, Base, BNB, Polygon, Arbitrum' : CHAINS.sol.net[w.settings.net].label;

  return (
    <Screen>
      <T kind="title">Receive</T>
      {!w.accounts.length ? <T kind="muted">Connect a device first.</T> : null}
      <Choice
        options={w.accounts.map((a) => ({ value: accountId(a), label: `${SCHEME_LABEL[a.scheme]}${a.account ? ` #${a.account}` : ''}` }))}
        value={sel ?? ''}
        onChange={(v) => {
          setSel(v);
          setVerified(null);
        }}
      />
      {acct ? (
        <Card>
          <T kind="muted">Works on: {chainsFor(acct.scheme)}</T>
          {verified?.id === accountId(acct) ? (
            <View style={{ alignItems: 'center', gap: 12 }}>
              <T kind="ok">✓ Verified on device</T>
              <View style={{ backgroundColor: '#fff', padding: 12, borderRadius: 8 }}>
                <QRCode value={verified.address} size={200} />
              </View>
              <T kind="mono" selectable center>{verified.address}</T>
              {acct.family === 'btc' ? <T kind="muted">Address #{verified.index}. A fresh address is used each time for privacy.</T> : null}
              <Button kind="secondary" title="Copy address" onPress={() => void Clipboard.setStringAsync(verified.address)} />
            </View>
          ) : (
            <>
              <T>The address is shown here only after your device displays it and you confirm it matches.</T>
              <Button title={w.device ? 'Show address on device' : 'Connect device first'} onPress={verify} loading={busy} disabled={!w.device} />
            </>
          )}
          {err ? <T kind="error">{err}</T> : null}
        </Card>
      ) : null}
    </Screen>
  );
}
