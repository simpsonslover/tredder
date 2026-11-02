import Ionicons from '@expo/vector-icons/Ionicons';
import { router } from 'expo-router';
import { useMemo, useState } from 'react';
import { View } from 'react-native';

import { CHAINS } from '../../chains/config';
import { assetKey, type Holding } from '../../chains/types';
import { useWallet } from '../../state/WalletContext';
import { assetLogo, chainLogo } from '../../ui/coinLogos';
import { ActionButton, AllocationBar, Badge, Button, Card, CoinIcon, Row, Screen, Segmented, T, coinColor, useTheme } from '../../ui/components';
import { formatAmount, formatUsd } from '../../util/amount';

const KIND_LABEL: Partial<Record<Holding['kind'], string>> = { staked: 'staked', nft: 'NFT', lp: 'LP' };

/** Same asset on the same chain, summed over every account that holds it. */
type Asset = { key: string; chain: Holding['chain']; asset: Holding['asset']; symbol: string; name: string; decimals: number; kind: Holding['kind']; amount: bigint; usd?: number; accounts: number };

function groupAssets(hs: Holding[]): Asset[] {
  const m = new Map<string, Asset>();
  for (const h of hs) {
    const k = `${assetKey(h)}|${h.kind}`;
    const a = m.get(k);
    if (a) {
      a.amount += h.amount;
      a.usd = a.usd === undefined && h.usdValue === undefined ? undefined : (a.usd ?? 0) + (h.usdValue ?? 0);
      a.accounts++;
    } else {
      m.set(k, { key: k, chain: h.chain, asset: h.asset, symbol: h.symbol, name: h.name, decimals: h.decimals, kind: h.kind, amount: h.amount, usd: h.usdValue, accounts: 1 });
    }
  }
  return [...m.values()].sort((a, b) => (b.usd ?? 0) - (a.usd ?? 0) || Number(b.amount > a.amount) - Number(b.amount < a.amount));
}

export default function Portfolio() {
  const w = useWallet();
  const t = useTheme();
  const [view, setView] = useState<'assets' | 'accounts'>('assets');
  const testnet = w.settings.net === 'test';
  const holdings = useMemo(() => w.portfolio?.holdings ?? [], [w.portfolio]);
  const assets = useMemo(() => groupAssets(holdings), [holdings]);
  const chainLabel = (c: Holding['chain']) => CHAINS[c].net[w.settings.net].label;

  const header = (
    <View style={{ flexDirection: 'row', justifyContent: 'space-between', alignItems: 'center' }}>
      <T kind="title">Wallet</T>
      <View style={{ flexDirection: 'row', alignItems: 'center', gap: 14 }}>
        {testnet ? <Badge label="TESTNET" color={t.warn} /> : <Badge label="MAINNET" color={t.ok} />}
      </View>
    </View>
  );

  if (!w.accounts.length)
    return (
      <Screen>
        {header}
        <Card style={{ alignItems: 'center', paddingVertical: 32, gap: 14 }}>
          <View style={{ width: 72, height: 72, borderRadius: 36, backgroundColor: t.accent + '26', alignItems: 'center', justifyContent: 'center' }}>
            <Ionicons name="hardware-chip-outline" size={34} color={t.accent} />
          </View>
          <T kind="h2" center>Connect your tredder</T>
          <T kind="muted" center>Your keys never leave the device. This app only reads public addresses and asks the device to sign.</T>
          <Button title="Connect device" icon="bluetooth" onPress={() => router.push('/device')} />
        </Card>
      </Screen>
    );

  return (
    <Screen onRefresh={w.refresh} refreshing={w.loading}>
      {header}

      <View style={{ gap: 4, paddingTop: 6 }}>
        <T kind="muted">{testnet ? 'Testnet portfolio' : 'Total balance'}</T>
        <T kind="hero">{testnet ? `${assets.length} asset${assets.length === 1 ? '' : 's'}` : formatUsd(w.portfolio?.totalUsd)}</T>
        <T kind="muted">
          {testnet ? 'Test coins have no USD value · ' : ''}
          {w.loading ? 'Updating…' : w.portfolio ? `Updated ${new Date(w.portfolio.updatedAt).toLocaleTimeString()}` : 'Pull down to load'}
          {w.device ? '' : ' · device not connected (read-only)'}
        </T>
      </View>

      <View style={{ flexDirection: 'row', paddingVertical: 6 }}>
        <ActionButton icon="arrow-up" label="Send" onPress={() => router.push('/send')} />
        <ActionButton icon="arrow-down" label="Receive" onPress={() => router.push('/receive')} />
        <ActionButton icon="shuffle" label="Sweep" onPress={() => router.push('/sweep')} />
        <ActionButton icon="search" label="Find" onPress={() => router.push('/import')} />
      </View>

      {!testnet ? <AllocationBar parts={assets.map((a) => ({ key: a.key, value: a.usd ?? 0, color: coinColor(a.symbol) }))} /> : null}

      {w.error ? <T kind="error">{w.error}</T> : null}
      {w.portfolio?.errors.length ? (
        <Card style={{ flexDirection: 'row', gap: 10, paddingVertical: 12 }}>
          <Ionicons name="warning-outline" size={18} color={t.warn} />
          <View style={{ flex: 1, gap: 4 }}>
            <T kind="muted">Some balances could not be loaded. Pull down to retry.</T>
            {w.portfolio.errors.map((e) => (
              <T key={e} kind="muted">• {e}</T>
            ))}
          </View>
        </Card>
      ) : null}

      <Segmented
        options={[{ value: 'assets', label: 'Assets' }, { value: 'accounts', label: 'Accounts' }]}
        value={view}
        onChange={setView}
      />

      {!holdings.length ? <T kind="muted" center>{w.loading ? 'Loading balances…' : 'No balances yet. Pull down to refresh.'}</T> : null}

      {view === 'assets' ? (
        <View>
          {assets.map((a) => (
            <Row
              key={a.key}
              onPress={a.kind === 'native' || a.kind === 'token' ? () => router.push({ pathname: '/send', params: { asset: assetKey(a), at: String(Date.now()) } }) : undefined}
              icon={
                <CoinIcon
                  symbol={a.symbol}
                  logo={assetLogo(a)}
                  small={a.kind !== 'native' || (a.chain !== 'eth' && CHAINS[a.chain].symbol === 'ETH') ? CHAINS[a.chain].name : undefined}
                  smallLogo={chainLogo(a.chain)}
                />
              }
              left={<T kind="h2">{a.name}{KIND_LABEL[a.kind] ? ` (${KIND_LABEL[a.kind]})` : ''}</T>}
              sub={`${chainLabel(a.chain)}${a.accounts > 1 ? ` · ${a.accounts} accounts` : ''}`}
              right={
                <>
                  <T kind="h2">{testnet ? `${formatAmount(a.amount, a.decimals, 6)}` : formatUsd(a.usd)}</T>
                  <T kind="muted">{testnet ? a.symbol : `${formatAmount(a.amount, a.decimals, 6)} ${a.symbol}`}</T>
                </>
              }
            />
          ))}
        </View>
      ) : (
        <View>
          {holdings.map((h, i) => (
            <Row
              key={`${h.owner}-${h.symbol}-${i}`}
              icon={<CoinIcon symbol={h.symbol} size={36} logo={assetLogo(h)} small={CHAINS[h.chain].name} smallLogo={chainLogo(h.chain)} />}
              left={<T kind="h2">{`${chainLabel(h.chain)} ${h.account ? `#${h.account + 1}` : ''}`.trim()}</T>}
              sub={h.chain === 'btc' ? `${h.owner} · ${h.scheme}` : `${h.owner.slice(0, 6)}…${h.owner.slice(-4)} · ${h.scheme}`}
              right={
                <>
                  <T>{`${formatAmount(h.amount, h.decimals, 6)} ${h.symbol}`}</T>
                  {!testnet ? <T kind="muted">{formatUsd(h.usdValue)}</T> : null}
                </>
              }
            />
          ))}
        </View>
      )}
    </Screen>
  );
}
