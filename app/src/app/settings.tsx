import { useState } from 'react';
import { Alert, Switch, Text, TextInput, View } from 'react-native';

import type { ChainId } from '../chains/config';
import { removeJson, secureKV } from '../state/storage';
import { useWallet } from '../state/WalletContext';
import { LARP_COIN_LIST } from '../state/wallet';
import { chainLogo } from '../ui/coinLogos';
import { Choice, CoinIcon, ListGroup, ListItem, MONO, Row, Screen, T, useTheme } from '../ui/components';

export default function Settings() {
  const w = useWallet();
  const t = useTheme();
  const s = w.settings;

  // Local text state for larp amount inputs -- committed to settings on blur to avoid
  // rebuilding the portfolio on every keystroke.
  const [larpText, setLarpText] = useState<Record<string, string>>(() =>
    Object.fromEntries(s.larpCoins.map((c) => [c.chain, String(c.amount)])),
  );

  const commitLarpCoin = (chain: ChainId, text: string) => {
    const n = parseFloat(text);
    const coins = (s.larpCoins ?? []).filter((c) => c.chain !== chain);
    if (!isNaN(n) && n > 0) coins.push({ chain, amount: n });
    void w.updateSettings({ larpCoins: coins });
  };

  const enableMainnet = () =>
    Alert.alert(
      'Enable mainnet?',
      'Mainnet uses real funds. Only enable it after you have tested receiving, sending and sweeping on testnet with this device.',
      [
        { text: 'Cancel', style: 'cancel' },
        { text: 'Enable', style: 'destructive', onPress: () => void w.updateSettings({ mainnetEnabled: true }) },
      ],
    );

  const forget = () =>
    Alert.alert('Forget accounts?', 'Removes public account data from this phone. Your funds and keys are on the device and are not affected.', [
      { text: 'Cancel', style: 'cancel' },
      {
        text: 'Forget',
        style: 'destructive',
        onPress: async () => {
          await w.setAccounts([]);
          await removeJson(secureKV, 'sweep.active');
        },
      },
    ]);

  return (
    <Screen>
      <ListGroup
        title="Network"
        footer={s.mainnetEnabled ? undefined : 'Testnets: Bitcoin testnet4, Sepolia, Base Sepolia, BNB testnet, Polygon Amoy, Arbitrum Sepolia, Solana devnet.'}
      >
        <ListItem
          icon="globe"
          color={s.net === 'test' ? t.warn : t.ok}
          title={s.net === 'test' ? 'Testnets' : 'Mainnet'}
          sub={s.net === 'test' ? 'Test coins, no real value' : 'Real funds'}
        />
        {s.mainnetEnabled ? (
          <View style={{ paddingVertical: 12 }}>
            <Choice options={[{ value: 'test', label: 'Testnets' }, { value: 'main', label: 'Mainnet' }]} value={s.net} onChange={(net) => void w.updateSettings({ net })} />
          </View>
        ) : null}
        {s.mainnetEnabled ? (
          <ListItem icon="shield" color={t.muted} title="Disable mainnet" onPress={() => void w.updateSettings({ mainnetEnabled: false })} />
        ) : (
          <ListItem icon="flash" color={t.ok} title="Enable mainnet…" onPress={enableMainnet} />
        )}
      </ListGroup>

      <ListGroup title="Sweep" footer="Tokens worth less than this are left behind when sweeping an old seed.">
        <View style={{ paddingVertical: 14, gap: 10 }}>
          <T kind="h2">Ignore tokens worth less than</T>
          <Choice
            options={[0, 1, 5, 20].map((v) => ({ value: String(v), label: `$${v}` }))}
            value={String(s.dustUsd)}
            onChange={(v) => void w.updateSettings({ dustUsd: Number(v) })}
          />
        </View>
      </ListGroup>

      <ListGroup
        title="Privacy"
        footer="This app stores only public data (account public keys and addresses) in the phone's secure storage, excluded from backups. Balances come from public APIs (mempool.space, Blockscout, public RPC nodes), which can see which addresses you look up. Prices come from Coinbase, Kraken and Pyth (read on Solana), always asked for the same full list so they reveal nothing about what you hold; meme coins none of them lists are priced by DexScreener, which sees those tokens."
      >
        <ListItem icon="trash" danger title="Forget all accounts on this phone" onPress={forget} />
      </ListGroup>

      <ListGroup
        title="LARP mode"
        footer={
          s.larpMode
            ? 'Prices are real (live from Coinbase/Kraken/Pyth). Balances are made up. No receive address or send is available in this mode.'
            : 'Show off a fake balance with real market prices. No real wallet needed.'
        }
      >
        <ListItem
          icon="glasses-outline"
          title="LARP mode"
          sub={s.larpMode ? 'On -- fake balances, real prices' : 'Off'}
          value={
            <Switch
              value={s.larpMode}
              onValueChange={(v) => void w.updateSettings({ larpMode: v })}
              thumbColor={t.text}
              trackColor={{ false: t.card2, true: t.accent2 }}
            />
          }
          chevron={false}
        />
        {s.larpMode
          ? LARP_COIN_LIST.map((coin) => {
              const val = larpText[coin.chain] ?? '';
              return (
                <Row
                  key={coin.chain}
                  icon={<CoinIcon symbol={coin.symbol} logo={chainLogo(coin.chain as ChainId)} />}
                  left={
                    <View style={{ gap: 1 }}>
                      <Text style={{ color: t.text, fontSize: 15 }}>{coin.symbol}</Text>
                      <T kind="muted">{coin.name}</T>
                    </View>
                  }
                  right={
                    <TextInput
                      value={val}
                      onChangeText={(v) => setLarpText((cur) => ({ ...cur, [coin.chain]: v }))}
                      onBlur={() => commitLarpCoin(coin.chain as ChainId, val)}
                      keyboardType="decimal-pad"
                      placeholder="0"
                      placeholderTextColor={t.muted}
                      style={{
                        color: t.text, fontSize: 16, fontFamily: MONO, fontWeight: '600',
                        textAlign: 'right', minWidth: 80, padding: 6,
                        backgroundColor: t.darker, borderRadius: 8,
                      }}
                      returnKeyType="done"
                    />
                  }
                />
              );
            })
          : null}
      </ListGroup>
    </Screen>
  );
}
