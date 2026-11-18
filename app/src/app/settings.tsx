import { Alert, View } from 'react-native';

import { removeJson, secureKV } from '../state/storage';
import { useWallet } from '../state/WalletContext';
import { Choice, ListGroup, ListItem, Screen, T, useTheme } from '../ui/components';

export default function Settings() {
  const w = useWallet();
  const t = useTheme();
  const s = w.settings;

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
        footer="This app stores only public data (account public keys and addresses) in the phone’s secure storage, excluded from backups. Balances come from public APIs (mempool.space, Blockscout, public RPC nodes), which can see which addresses you look up. Prices come from Coinbase, Kraken and Pyth (read on Solana), always asked for the same full list so they reveal nothing about what you hold; meme coins none of them lists are priced by DexScreener, which sees those tokens."
      >
        <ListItem icon="trash" danger title="Forget all accounts on this phone" onPress={forget} />
      </ListGroup>
    </Screen>
  );
}
