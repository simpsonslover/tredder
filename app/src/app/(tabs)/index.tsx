import { router } from 'expo-router';
import { useMemo, useState } from 'react';
import { Pressable, Text, View } from 'react-native';

import { CHAINS } from '../../chains/config';
import { assetKey, type Holding } from '../../chains/types';
import { useWallet } from '../../state/WalletContext';
import { assetLogo, assetSeed, chainLogo } from '../../ui/coinLogos';
import { splitHoldings } from '../../ui/tokenTrust';
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
  const [showUnverified, setShowUnverified] = useState(false);
  const [showErrors, setShowErrors] = useState(false);
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
      <ListItem
        icon="list-outline"
        title={view === 'accounts' ? 'Show coins' : 'Show accounts and addresses'}
        onPress={() => (setMenu(false), setView(view === 'accounts' ? 'assets' : 'accounts'))}
      />
      <ListItem icon="settings-outline" title="Preferences" onPress={() => open('/settings')} />
    </Sheet>
  );
  const title = w.device ? w.device.name : 'tredder';

  if (!w.accounts.length)
    return (
      <Screen title={title} icon="wallet" right={toolbarRight}>
        <Pane hero style={{ alignItems: 'center', paddingVertical: 32, gap: 12 }}>
          <Text style={{ color: t.text, fontSize: 23, fontWeight: '700' }}>No wallet loaded</Text>
          <T center kind="muted">Connect your tredder. Your keys never leave the device: this app only reads public addresses and asks the device to sign.</T>
        </Pane>
        <Button title="Connect device" icon="bluetooth" onPress={() => router.navigate('/device')} />
        {walletMenu}
      </Screen>
    );

  const split = splitHoldings(assets);
  const assetRow = (a: Asset) => (
    <Row
      key={a.key}
      onPress={a.kind === 'native' || a.kind === 'token' ? () => router.push({ pathname: '/send', params: { asset: assetKey(a), at: String(Date.now()) } }) : undefined}
      icon={
        <CoinIcon
          symbol={a.symbol}
          logo={assetLogo(a)}
          seed={assetSeed(a)}
          small={a.kind !== 'native' || (a.chain !== 'eth' && CHAINS[a.chain].symbol === 'ETH') ? CHAINS[a.chain].name : undefined}
          smallLogo={chainLogo(a.chain)}
        />
      }
      left={<Text style={{ color: t.text, fontSize: 17 }}>{a.kind === 'native' ? a.symbol : a.name}{KIND_LABEL[a.kind] ? ` (${KIND_LABEL[a.kind]})` : ''}</Text>}
      sub={`${chainLabel(a.chain)}${a.accounts > 1 ? ` · ${a.accounts} accounts` : ''}`}
      right={
        <>
          <Text style={{ color: t.text, fontSize: 15, fontFamily: MONO, fontWeight: '700' }}>{formatAmount(a.amount, a.decimals, 6)}</Text>
          <T kind="muted">{testnet || a.usd === undefined ? a.symbol : formatUsd(a.usd)}</T>
        </>
      }
    />
  );
  const errors = w.portfolio?.errors ?? [];

  return (
    <Screen title={title} icon="wallet" right={toolbarRight} onRefresh={w.refresh} refreshing={w.loading}>
      <Pane hero style={{ paddingHorizontal: 22, paddingVertical: 20, gap: 10 }}>
        {testnet ? (
          <>
            <Text style={{ color: t.text, fontSize: 24, fontWeight: '700' }}>Test wallet</Text>
            <T kind="muted">Testnet coins have no real value.</T>
          </>
        ) : (
          <>
            <Text style={{ color: t.accent2, fontSize: 11.5, fontWeight: '700', letterSpacing: 1.1, textTransform: 'uppercase' }}>Total balance</Text>
            <Text style={{ color: t.text, fontSize: 40, fontFamily: MONO, letterSpacing: -1.2 }}>{formatUsd(w.portfolio?.totalUsd)}</Text>
            <AllocationBar parts={assets.map((a) => ({ key: a.key, value: a.usd ?? 0, color: coinColor(a.symbol) }))} />
          </>
        )}
        <T kind="muted">
          {w.loading ? 'Updating…' : w.portfolio ? `Updated ${new Date(w.portfolio.updatedAt).toLocaleTimeString()} · pull down to refresh` : 'Pull down to load'}
          {w.device ? '' : ' · device not connected'}
        </T>
      </Pane>

      {w.error ? <Notice kind="error">{w.error}</Notice> : null}
      {errors.length ? (
        <Pressable onPress={() => setShowErrors((v) => !v)}>
          <Notice>
            <T kind="warn">{errors.length === 1 ? '1 network' : `${errors.length} networks`} could not be loaded · {showErrors ? 'hide' : 'details'}</T>
            {showErrors ? errors.map((e) => <T key={e} kind="muted">• {e}</T>) : null}
          </Notice>
        </Pressable>
      ) : null}

      {view === 'accounts' ? (
        <ListGroup title="Accounts and addresses" footer="Every account on every network, as the device reported it.">
          {holdings.map((h, i) => (
            <Row
              key={`${h.owner}-${h.symbol}-${i}`}
              icon={<CoinIcon symbol={h.symbol} logo={assetLogo(h)} seed={assetSeed(h)} small={CHAINS[h.chain].name} smallLogo={chainLogo(h.chain)} />}
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
      ) : !split.coins.length && !split.tokens.length && !split.unverified.length ? (
        <Pane style={{ alignItems: 'center', paddingVertical: 24, gap: 10 }}>
          <T center kind="muted">{w.loading ? 'Loading balances…' : 'No coins yet.'}</T>
          {!w.loading ? <Button kind="secondary" title="Receive coins" icon="arrow-down" onPress={() => router.navigate('/receive')} /> : null}
        </Pane>
      ) : (
        <>
          {split.coins.length ? <ListGroup title="Coins">{split.coins.map(assetRow)}</ListGroup> : null}
          {split.tokens.length ? <ListGroup title="Tokens">{split.tokens.map(assetRow)}</ListGroup> : null}
          {split.unverified.length ? (
            <ListGroup
              title={`Unverified tokens (${split.unverified.length})`}
              footer={showUnverified ? 'Anyone can send tokens to your address. Unknown ones are often scams that copy real names: do not trade or approve them.' : undefined}
            >
              <ListItem
                icon={showUnverified ? 'eye-off-outline' : 'eye-outline'}
                title={showUnverified ? 'Hide unverified tokens' : 'Show unverified tokens'}
                sub={showUnverified ? undefined : 'Airdrops, test tokens and look-alikes'}
                onPress={() => setShowUnverified((v) => !v)}
                chevron={false}
              />
              {showUnverified ? split.unverified.map(assetRow) : null}
            </ListGroup>
          ) : null}
        </>
      )}
      {walletMenu}
    </Screen>
  );
}
