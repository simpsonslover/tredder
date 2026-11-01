import { Linking } from 'react-native';

import { CHAINS, explorerTx } from '../../chains/config';
import { useWallet } from '../../state/WalletContext';
import { Card, Row, Screen, T } from '../../ui/components';

export default function Activity() {
  const w = useWallet();
  return (
    <Screen onRefresh={w.refresh} refreshing={w.loading}>
      <T kind="title">Activity</T>
      {!w.history.length ? <T kind="muted">{w.loading ? 'Loading…' : 'No transactions yet.'}</T> : null}
      <Card>
        {w.history.slice(0, 100).map((tx) => (
          <Row
            key={tx.chain + tx.hash}
            onPress={() => void Linking.openURL(explorerTx(tx.chain, w.settings.net, tx.hash))}
            left={`${tx.direction === 'in' ? '↓ Received' : tx.direction === 'out' ? '↑ Sent' : '• Activity'} · ${CHAINS[tx.chain].net[w.settings.net].label}`}
            sub={`${tx.time ? new Date(tx.time * 1000).toLocaleString() : 'pending'} · ${tx.hash.slice(0, 10)}…`}
            right={
              <>
                {tx.amount ? <T>{tx.amount}</T> : null}
                <T kind="muted">
                  <T kind={tx.status === 'failed' ? 'error' : 'muted'}>{tx.status}</T>
                </T>
              </>
            }
          />
        ))}
      </Card>
      <T kind="muted">Tap a transaction to open it in the block explorer.</T>
    </Screen>
  );
}
