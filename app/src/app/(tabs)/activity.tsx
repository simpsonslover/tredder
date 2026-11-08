import Ionicons from '@expo/vector-icons/Ionicons';
import { Linking, Text, View } from 'react-native';

import { CHAINS, explorerTx } from '../../chains/config';
import type { TxRecord } from '../../chains/types';
import { useWallet } from '../../state/WalletContext';
import { ListGroup, MONO, NetworkTag, Pane, Row, Screen, T, useTheme, type IconName } from '../../ui/components';

const DIR: Record<TxRecord['direction'], { label: string; icon: IconName }> = {
  in: { label: 'Received', icon: 'arrow-down' },
  out: { label: 'Sent', icon: 'arrow-up' },
  self: { label: 'Self-transfer', icon: 'swap-horizontal' },
};

/* Electrum's history sections */
const SECTIONS = ['pending', 'today', 'yesterday', 'lastweek', 'lastmonth', 'older'] as const;
const SECTION_LABEL: Record<(typeof SECTIONS)[number], string> = {
  pending: 'Pending',
  today: 'Today',
  yesterday: 'Yesterday',
  lastweek: 'Last week',
  lastmonth: 'Last month',
  older: 'Older',
};

function sectionOf(tx: TxRecord, now: Date): (typeof SECTIONS)[number] {
  if (!tx.time || tx.status === 'pending') return 'pending';
  const day = (d: Date) => new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime();
  const days = Math.round((day(now) - day(new Date(tx.time * 1000))) / 86_400_000);
  return days <= 0 ? 'today' : days === 1 ? 'yesterday' : days < 7 ? 'lastweek' : days < 31 ? 'lastmonth' : 'older';
}

export default function Activity() {
  const w = useWallet();
  const t = useTheme();
  const now = new Date();
  const txs = w.history.slice(0, 100);
  const groups = SECTIONS.map((s) => ({ s, items: txs.filter((tx) => sectionOf(tx, now) === s) })).filter((g) => g.items.length);

  return (
    <Screen title="History" icon="time" right={w.settings.net === 'test' ? <NetworkTag label="testnet" /> : undefined} onRefresh={w.refresh} refreshing={w.loading}>
      {!txs.length ? (
        <Pane style={{ alignItems: 'center', paddingVertical: 28, gap: 8 }}>
          <Text style={{ color: t.accent, fontSize: 18 }}>{w.loading ? 'Synchronizing…' : 'No transactions yet'}</Text>
          <T kind="muted" center>Payments you send or receive show up here.</T>
        </Pane>
      ) : null}
      {groups.map(({ s, items }) => (
        <ListGroup key={s} title={SECTION_LABEL[s]}>
          {items.map((tx) => {
            const d = DIR[tx.direction];
            const failed = tx.status === 'failed';
            const color = failed ? t.muted : tx.direction === 'in' ? t.ok : tx.direction === 'out' ? t.danger : t.text;
            return (
              <Row
                key={tx.chain + tx.hash}
                onPress={() => void Linking.openURL(explorerTx(tx.chain, w.settings.net, tx.hash))}
                icon={
                  <View style={{ width: 32, height: 32, alignItems: 'center', justifyContent: 'center' }}>
                    <Ionicons name={tx.status === 'pending' ? 'time-outline' : failed ? 'close-circle-outline' : 'checkmark-done'} size={24} color={tx.status === 'pending' ? t.warn : failed ? t.danger : t.muted} />
                  </View>
                }
                left={
                  <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6 }}>
                    <Ionicons name={d.icon} size={15} color={color} />
                    <Text style={{ color: t.text, fontSize: 17 }}>{d.label}</Text>
                  </View>
                }
                sub={`${CHAINS[tx.chain].net[w.settings.net].label} · ${tx.time ? new Date(tx.time * 1000).toLocaleString() : 'unconfirmed'}`}
                right={
                  <>
                    {tx.amount ? (
                      <Text style={{ color, fontSize: 15, fontFamily: MONO, fontWeight: '700' }}>{`${tx.direction === 'in' ? '+' : tx.direction === 'out' ? '-' : ''}${tx.amount}`}</Text>
                    ) : null}
                    <T kind={failed ? 'error' : 'muted'}>{tx.status}</T>
                  </>
                }
              />
            );
          })}
        </ListGroup>
      ))}
      {txs.length ? <T kind="muted" center>Tap a transaction to open it in the block explorer.</T> : null}
    </Screen>
  );
}
