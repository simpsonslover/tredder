/*
 * Receive: pick a network, the tredder shows the address, then the app shows the QR.
 * An address is only displayed after the device has shown it and you confirmed it
 * matches: a compromised phone cannot swap in its own address (security over speed).
 */
import Ionicons from '@expo/vector-icons/Ionicons';
import * as Clipboard from 'expo-clipboard';
import { router, useFocusEffect } from 'expo-router';
import { useCallback, useState } from 'react';
import { BackHandler, Share, View } from 'react-native';
import QRCode from 'react-native-qrcode-svg';

import { CHAINS, SCHEME_LABEL } from '../../chains/config';
import { DeviceError } from '../../protocol/types';
import { accountId, expectedReceiveAddress, verifyReceiveAddress, type WalletAccount } from '../../state/wallet';
import { useWallet } from '../../state/WalletContext';
import { chainLogo } from '../../ui/coinLogos';
import { Button, Card, CoinIcon, ListGroup, Row, T, useTheme } from '../../ui/components';
import { AddressText, IconPill, StepScreen } from '../../ui/flow';

/** What a person calls each account, and which networks it receives on. */
function describe(a: WalletAccount, net: 'main' | 'test') {
  if (a.family === 'btc')
    return a.scheme === 'btc-p2tr'
      ? { name: 'Bitcoin, Taproot address', logo: chainLogo('btc'), symbol: 'BTC', networks: 'Newer address type. Use the Bitcoin one above if unsure.', only: 'Bitcoin' }
      : { name: 'Bitcoin', logo: chainLogo('btc'), symbol: 'BTC', networks: CHAINS.btc.net[net].label, only: 'Bitcoin' };
  if (a.family === 'evm') {
    const list = (['eth', 'base', 'arbitrum', 'polygon', 'bnb'] as const).map((c) => CHAINS[c].net[net].label);
    return { name: 'Ethereum & tokens', logo: chainLogo('eth'), symbol: 'ETH', networks: 'Same address on Base, Arbitrum, Polygon and BNB Chain', only: 'Ethereum and EVM-network (' + list.join(', ') + ')' };
  }
  return { name: 'Solana & tokens', logo: chainLogo('sol'), symbol: 'SOL', networks: 'SOL and Solana tokens', only: 'Solana' };
}

export default function Receive() {
  const w = useWallet();
  const t = useTheme();
  const net = w.settings.net;
  const [sel, setSel] = useState<WalletAccount | null>(null);
  const [shown, setShown] = useState<{ id: string; address: string; index: number } | null>(null);
  const [busy, setBusy] = useState(false);
  const [err, setErr] = useState<string | null>(null);
  const [copied, setCopied] = useState(false);

  const back = useCallback(() => {
    if (sel) {
      setSel(null);
      setShown(null);
      setErr(null);
    } else router.navigate('/');
    return true;
  }, [sel]);
  useFocusEffect(
    useCallback(() => {
      const sub = BackHandler.addEventListener('hardwareBackPress', back);
      return () => sub.remove();
    }, [back]),
  );

  const show = async (a: WalletAccount) => {
    setSel(a);
    setErr(null);
    setCopied(false);
    if (shown?.id === accountId(a)) return;
    setShown(null);
    if (!w.device) return;
    setBusy(true);
    try {
      const r = await verifyReceiveAddress(w.device.client, a, net, w.portfolio);
      setShown({ id: accountId(a), ...r });
    } catch (e) {
      setErr(e instanceof DeviceError && e.code === 'USER_REJECTED' ? 'You pressed Back on the device. The address was not confirmed, so it is not shown.' : (e as Error).message);
    } finally {
      setBusy(false);
    }
  };

  if (w.larpMode)
    return (
      <StepScreen title="Receive" onBack={back}>
        <Card style={{ alignItems: 'center', gap: 16, paddingVertical: 32 }}>
          <Ionicons name="glasses-outline" size={40} color={t.muted} />
          <T kind="h2" center>LARP mode</T>
          <T kind="muted" center>No receive address in LARP mode — there is no real wallet behind these balances.</T>
          <Button kind="secondary" title="Turn off LARP mode" onPress={() => router.push('/settings')} />
        </Card>
      </StepScreen>
    );

  if (!sel)
    return (
      <StepScreen title="Receive" onBack={back}>
        {!w.accounts.length ? (
          <Card style={{ alignItems: 'center', gap: 12, paddingVertical: 28 }}>
            <Ionicons name="shield-checkmark-outline" size={34} color={t.muted} />
            <T kind="h2" center>Connect your tredder</T>
            <T kind="muted" center>Your addresses come from the device.</T>
            <Button kind="secondary" title="Go to Device" onPress={() => router.navigate('/device')} />
          </Card>
        ) : (
          <>
            <ListGroup title="What do you want to receive?" footer={net === 'test' ? 'Testnet addresses: only send test coins here.' : 'The address is checked on your tredder before it is shown.'}>
              {[...w.accounts].sort((x, y) => Number(x.scheme === 'btc-p2tr') - Number(y.scheme === 'btc-p2tr')).map((a) => {
                const d = describe(a, net);
                return (
                  <Row
                    key={accountId(a)}
                    onPress={() => void show(a)}
                    icon={<CoinIcon symbol={d.symbol} logo={d.logo} />}
                    left={<T kind="h2">{d.name + (a.account ? ` #${a.account + 1}` : '')}</T>}
                    sub={d.networks}
                    right={
                      <View style={{ width: 36, height: 36, borderRadius: 12, backgroundColor: t.accent + '24', alignItems: 'center', justifyContent: 'center' }}>
                        <Ionicons name="qr-code" size={18} color={t.accent} />
                      </View>
                    }
                  />
                );
              })}
            </ListGroup>
          </>
        )}
      </StepScreen>
    );

  const d = describe(sel, net);
  const ok = shown?.id === accountId(sel);
  return (
    <StepScreen
      title={`Receive ${d.symbol}`}
      onBack={back}
      footer={
        ok ? (
          <View style={{ flexDirection: 'row', gap: 10 }}>
            <View style={{ flex: 1 }}>
              <Button
                kind="secondary"
                icon={copied ? 'checkmark' : 'copy-outline'}
                title={copied ? 'Copied' : 'Copy'}
                onPress={() => void Clipboard.setStringAsync(shown!.address).then(() => setCopied(true))}
              />
            </View>
            <View style={{ flex: 1 }}>
              <Button icon="share-outline" title="Share" onPress={() => void Share.share({ message: shown!.address })} />
            </View>
          </View>
        ) : !w.device ? (
          <Button title="Connect your tredder" icon="bluetooth" onPress={() => router.navigate('/device')} />
        ) : err ? (
          <Button title="Show on device again" onPress={() => void show(sel)} />
        ) : null
      }
    >
      {ok ? (
        <View style={{ alignItems: 'center', gap: 16 }}>
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
            <Ionicons name="shield-checkmark" size={16} color={t.ok} />
            <T kind="ok">Confirmed on your tredder</T>
          </View>
          <View style={{ backgroundColor: '#fff', padding: 16, borderRadius: 20 }}>
            <QRCode value={shown!.address} size={220} ecl="H" logo={d.logo} logoSize={44} logoBackgroundColor="#fff" logoMargin={4} logoBorderRadius={22} />
          </View>
          <Card style={{ alignSelf: 'stretch' }}>
            <AddressText address={shown!.address} center size={16} />
          </Card>
          <View style={{ flexDirection: 'row', gap: 10, backgroundColor: t.warn + '1A', borderRadius: 14, padding: 14, alignSelf: 'stretch' }}>
            <Ionicons name="warning-outline" size={20} color={t.warn} />
            <View style={{ flex: 1 }}>
              <T>Only send {d.only} assets to this address.</T>
              <T kind="muted">Coins sent from another network can be lost.</T>
            </View>
          </View>
          {sel.family === 'btc' ? <T kind="muted" center>A new address is used each time for privacy. Older ones still work.</T> : null}
        </View>
      ) : w.device && busy && w.awaitingKind === 'show-address' ? (
        <View style={{ gap: 16, paddingTop: 10 }}>
          <View style={{ alignItems: 'center', gap: 12 }}>
            <View style={{ width: 72, height: 72, borderRadius: 36, backgroundColor: t.accent + '26', alignItems: 'center', justifyContent: 'center' }}>
              <Ionicons name="shield-checkmark-outline" size={34} color={t.accent} />
            </View>
            <T kind="h2" center>Compare with your tredder</T>
            <T kind="muted" center>Your tredder is showing your address. It must match this one exactly.</T>
          </View>
          <Card>
            <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
              <Ionicons name="time-outline" size={14} color={t.warn} />
              <T kind="warn">Not verified yet</T>
            </View>
            <AddressText address={expectedReceiveAddress(sel, net, w.portfolio).address} center size={16} selectable={false} />
          </Card>
          <View style={{ flexDirection: 'row', gap: 10, backgroundColor: t.accent + '1A', borderRadius: 14, padding: 14 }}>
            <Ionicons name="shield-checkmark-outline" size={20} color={t.accent} />
            <View style={{ flex: 1 }}>
              <T>Press Confirm on the tredder only if they match.</T>
              <T kind="muted">If anything differs, press Back on the tredder. The QR code, Copy and Share appear only after you confirm.</T>
            </View>
          </View>
        </View>
      ) : (
        <View style={{ flex: 1, alignItems: 'center', justifyContent: 'center', gap: 16, paddingTop: 30 }}>
          <View style={{ width: 84, height: 84, borderRadius: 42, backgroundColor: t.accent + '26', alignItems: 'center', justifyContent: 'center' }}>
            <Ionicons name="shield-checkmark-outline" size={40} color={t.accent} />
          </View>
          <T kind="h2" center>{!w.device ? 'Connect your tredder' : busy ? 'Check your tredder' : err ? 'Not confirmed' : 'Preparing…'}</T>
          <T kind="muted" center>
            {!w.device
              ? 'The address is shown here only after the device has shown it to you.'
              : busy
                ? 'It is showing your address. Press Confirm on the device to show it here too.'
                : err ?? ''}
          </T>
          {err ? <IconPill icon="arrow-back" label="Choose another network" onPress={back} /> : null}
        </View>
      )}
    </StepScreen>
  );
}
