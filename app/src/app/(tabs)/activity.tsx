import Ionicons from '@expo/vector-icons/Ionicons';
import { Linking, View } from 'react-native';

import { CHAINS, explorerTx } from '../../chains/config';
import type { TxRecord } from '../../chains/types';
import { useWallet } from '../../state/WalletContext';
import { Row, Screen, T, useTheme, type IconName } from '../../ui/components';

const DIR: Record<TxRecord['direction'], { label: string; icon: IconName }> = {
  in: { label: 'Received', icon: 'arrow-down' },
  out: { label: 'Sent', icon: 'arrow-up' },
  self: { label: 'Activity', icon: 'swap-horizontal' },
};

export default function Activity() {
  const w = useWallet();
  const t = useTheme();
  return (
    <Screen onRefresh={w.refresh} refreshing={w.loading}>
      <T kind="title">Activity</T>
      {!w.history.length ? (
        <View style={{ alignItems: 'center', gap: 10, paddingVertical: 48 }}>
          <Ionicons name="time-outline" size={40} color={t.muted} />
          <T kind="muted" center>{w.loading ? 'Loading…' : 'No transactions yet.'}</T>
        </View>
      ) : null}
      <View>
        {w.history.slice(0, 100).map((tx) => {
          const d = DIR[tx.direction];
          const color = tx.status === 'failed' ? t.danger : tx.direction === 'in' ? t.ok : t.text;
          return (
            <Row
              key={tx.chain + tx.hash}
              onPress={() => void Linking.openURL(explorerTx(tx.chain, w.settings.net, tx.hash))}
              icon={
                <View style={{ width: 40, height: 40, borderRadius: 20, backgroundColor: t.card2, alignItems: 'center', justifyContent: 'center' }}>
                  <Ionicons name={d.icon} size={18} color={color} />
                </View>
              }
              left={<T kind="h2">{d.label}</T>}
              sub={`${CHAINS[tx.chain].net[w.settings.net].label} · ${tx.time ? new Date(tx.time * 1000).toLocaleString() : 'pending'}`}
              right={
                <>
                  {tx.amount ? <T>{`${tx.direction === 'in' ? '+' : tx.direction === 'out' ? '−' : ''}${tx.amount}`}</T> : null}
                  <T kind={tx.status === 'failed' ? 'error' : 'muted'}>{tx.status}</T>
                </>
              }
            />
          );
        })}
      </View>
      {w.history.length ? <T kind="muted" center>Tap a transaction to open it in the block explorer.</T> : null}
    </Screen>
  );
}
