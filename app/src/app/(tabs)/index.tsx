import { router } from 'expo-router';
import { useMemo, useState } from 'react';
import { Text, View } from 'react-native';

import { CHAINS } from '../../chains/config';
import { assetKey, type Holding } from '../../chains/types';
import { useWallet } from '../../state/WalletContext';
import { assetLogo, chainLogo } from '../../ui/coinLogos';
import {
  AllocationBar,
  Button,
  CoinIcon,
  KeyValue,
  ListGroup,
  ListItem,
  MONO,
  NetworkTag,
  Notice,
  Pane,
  Row,
  Screen,
  Segmented,
  Sheet,
  T,
  ToolbarButton,
  coinColor,
  useTheme,
} from '../../ui/components';
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
  const [menu, setMenu] = useState(false);
  const testnet = w.settings.net === 'test';
  const holdings = useMemo(() => w.portfolio?.holdings ?? [], [w.portfolio]);
  const assets = useMemo(() => groupAssets(holdings), [holdings]);
  const chainLabel = (c: Holding['chain']) => CHAINS[c].net[w.settings.net].label;

  const open = (path: '/import' | '/sweep' | '/settings') => {
    setMenu(false);
    router.push(path);
  };
  const toolbarRight = (
    <View style={{ flexDirection: 'row', alignItems: 'center' }}>
      {testnet ? <NetworkTag label="testnet" /> : null}
      <ToolbarButton icon="ellipsis-vertical" label="Wallet menu" onPress={() => setMenu(true)} />
    </View>
  );
  const walletMenu = (
    <Sheet visible={menu} onClose={() => setMenu(false)} title="Wallet">
      <ListItem icon="search" title="Find accounts" sub="Look up an imported seed's balances" onPress={() => open('/import')} />
      <ListItem icon="shuffle" title="Sweep to new wallet" sub="Move an old seed's funds to this device" onPress={() => open('/sweep')} />
      <ListItem icon="settings-outline" title="Preferences" onPress={() => open('/settings')} />
    </Sheet>
  );
  const title = w.device ? w.device.name : 'tredder';

  if (!w.accounts.length)
    return (
      <Screen title={title} icon="wallet" right={toolbarRight}>
        <Pane style={{ alignItems: 'center', paddingVertical: 28, gap: 12 }}>
          <Text style={{ color: t.accent, fontSize: 22 }}>No wallet loaded</Text>
          <T center kind="muted">Connect your tredder. Your keys never leave the device: this app only reads public addresses and asks the device to sign.</T>
        </Pane>
        <Button title="Connect device" icon="bluetooth" onPress={() => router.navigate('/device')} />
        {walletMenu}
      </Screen>
    );

  return (
    <Screen title={title} icon="wallet" right={toolbarRight} onRefresh={w.refresh} refreshing={w.loading}>
      <Pane style={{ paddingHorizontal: 20, paddingVertical: 14, gap: 6 }}>
        {testnet ? (
          <KeyValue label="Assets" value={<Text style={{ color: t.text, fontSize: 22, fontFamily: MONO }}>{String(assets.length)}</Text>} />
        ) : (
          <KeyValue label="Balance" value={<Text style={{ color: t.text, fontSize: 22, fontFamily: MONO }}>{formatUsd(w.portfolio?.totalUsd).replace('$', '')}</Text>} unit="USD" />
        )}
        <T kind="muted">
          {testnet ? 'Testnet: coins have no value · ' : ''}
          {w.loading ? 'Synchronizing…' : w.portfolio ? `Updated ${new Date(w.portfolio.updatedAt).toLocaleTimeString()}` : 'Pull down to load'}
          {w.device ? '' : ' · watch-only (device not connected)'}
        </T>
        {!testnet ? <AllocationBar parts={assets.map((a) => ({ key: a.key, value: a.usd ?? 0, color: coinColor(a.symbol) }))} /> : null}
      </Pane>

      {w.error ? <Notice kind="error">{w.error}</Notice> : null}
      {w.portfolio?.errors.length ? (
        <Notice>
          <T kind="warn">Some balances could not be loaded. Pull down to retry.</T>
          {w.portfolio.errors.map((e) => (
            <T key={e} kind="muted">• {e}</T>
          ))}
        </Notice>
      ) : null}

      <Segmented
        options={[{ value: 'assets', label: 'Assets' }, { value: 'accounts', label: 'Accounts' }]}
        value={view}
        onChange={setView}
      />

      {!holdings.length ? <T kind="muted" center>{w.loading ? 'Loading balances…' : 'No balances yet. Pull down to refresh.'}</T> : null}

      {view === 'assets' && assets.length ? (
        <ListGroup>
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
              left={<Text style={{ color: t.text, fontSize: 17 }}>{a.name}{KIND_LABEL[a.kind] ? ` (${KIND_LABEL[a.kind]})` : ''}</Text>}
              sub={`${chainLabel(a.chain)}${a.accounts > 1 ? ` · ${a.accounts} accounts` : ''}`}
              right={
                <>
                  <Text style={{ color: t.text, fontSize: 15, fontFamily: MONO, fontWeight: '700' }}>{formatAmount(a.amount, a.decimals, 6)}</Text>
                  <T kind="muted">{testnet ? a.symbol : `${a.symbol} · ${formatUsd(a.usd)}`}</T>
                </>
              }
            />
          ))}
        </ListGroup>
      ) : view === 'accounts' && holdings.length ? (
        <ListGroup>
          {holdings.map((h, i) => (
            <Row
              key={`${h.owner}-${h.symbol}-${i}`}
              icon={<CoinIcon symbol={h.symbol} logo={assetLogo(h)} small={CHAINS[h.chain].name} smallLogo={chainLogo(h.chain)} />}
              left={<Text style={{ color: t.text, fontSize: 16 }}>{`${chainLabel(h.chain)} ${h.account ? `#${h.account + 1}` : ''}`.trim()}</Text>}
              sub={<Text style={{ color: t.muted, fontSize: 12, fontFamily: MONO }}>{h.chain === 'btc' ? `${h.owner} · ${h.scheme}` : `${h.owner.slice(0, 8)}…${h.owner.slice(-6)} · ${h.scheme}`}</Text>}
              right={
                <>
                  <Text style={{ color: t.text, fontSize: 14, fontFamily: MONO, fontWeight: '700' }}>{formatAmount(h.amount, h.decimals, 6)}</Text>
                  <T kind="muted">{testnet ? h.symbol : formatUsd(h.usdValue)}</T>
                </>
              }
            />
          ))}
        </ListGroup>
      ) : null}
      {walletMenu}
    </Screen>
  );
}
