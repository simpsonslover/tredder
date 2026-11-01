import { router } from 'expo-router';
import { View } from 'react-native';

import { CHAINS } from '../../chains/config';
import type { Holding } from '../../chains/types';
import { useWallet } from '../../state/WalletContext';
import { Badge, Button, Card, Row, Screen, T, useTheme } from '../../ui/components';
import { formatAmount, formatUsd } from '../../util/amount';

const KIND_LABEL: Partial<Record<Holding['kind'], string>> = { staked: 'staked', nft: 'NFT', lp: 'LP' };

export default function Portfolio() {
  const w = useWallet();
  const t = useTheme();
  const testnet = w.settings.net === 'test';

  if (!w.accounts.length)
    return (
      <Screen>
        <T kind="title">Portfolio</T>
        <Card>
          <T kind="h2">No wallet connected</T>
          <T kind="muted">Connect your hardware wallet to see balances. Your keys never leave the device. This app only reads public addresses and asks the device to sign.</T>
          <Button title="Connect device" onPress={() => router.push('/device')} />
        </Card>
      </Screen>
    );

  const byChain = new Map<string, Holding[]>();
  for (const h of w.portfolio?.holdings ?? []) byChain.set(h.chain, [...(byChain.get(h.chain) ?? []), h]);

  return (
    <Screen onRefresh={w.refresh} refreshing={w.loading}>
      <View style={{ flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}>
        <T kind="title">Portfolio</T>
        {testnet ? <Badge label="TESTNET" color={t.warn} /> : <Badge label="MAINNET" color={t.ok} />}
      </View>
      <Card>
        <T kind="muted">Total value</T>
        <T kind="title">{testnet ? 'Testnet: no USD value' : formatUsd(w.portfolio?.totalUsd)}</T>
        <T kind="muted">
          {w.loading ? 'Updating…' : w.portfolio ? `Updated ${new Date(w.portfolio.updatedAt).toLocaleTimeString()}` : 'Pull to load'}
          {w.device ? '' : '  ·  device not connected (read-only)'}
        </T>
      </Card>
      {w.error ? <T kind="error">{w.error}</T> : null}
      {w.portfolio?.errors.map((e) => (
        <T key={e} kind="warn">⚠ {e}</T>
      ))}
      {[...byChain.entries()].map(([chain, hs]) => (
        <Card key={chain}>
          <T kind="h2">{CHAINS[chain as keyof typeof CHAINS].net[w.settings.net].label}</T>
          {hs.map((h, i) => (
            <Row
              key={`${h.owner}-${h.symbol}-${i}`}
              left={`${h.symbol}${KIND_LABEL[h.kind] ? ` (${KIND_LABEL[h.kind]})` : ''}`}
              sub={h.chain === 'btc' ? h.owner : `${h.owner.slice(0, 6)}…${h.owner.slice(-4)} · ${h.scheme}`}
              right={
                <>
                  <T>{formatAmount(h.amount, h.decimals, 6)}</T>
                  <T kind="muted">{testnet ? '' : formatUsd(h.usdValue)}</T>
                </>
              }
            />
          ))}
        </Card>
      ))}
      <Button kind="secondary" title="Find more accounts (import scan)" onPress={() => router.push('/import')} />
    </Screen>
  );
}
