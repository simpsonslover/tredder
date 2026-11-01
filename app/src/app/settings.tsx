import { Alert } from 'react-native';

import { removeJson, secureKV } from '../state/storage';
import { useWallet } from '../state/WalletContext';
import { Button, Card, Choice, Screen, T } from '../ui/components';

export default function Settings() {
  const w = useWallet();
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

  return (
    <Screen>
      <Card>
        <T kind="h2">Network</T>
        {s.mainnetEnabled ? (
          <Choice options={[{ value: 'test', label: 'Testnets' }, { value: 'main', label: 'Mainnet' }]} value={s.net} onChange={(net) => void w.updateSettings({ net })} />
        ) : (
          <>
            <T kind="muted">Testnets: Bitcoin testnet4, Sepolia, Base Sepolia, BNB testnet, Polygon Amoy, Arbitrum Sepolia, Solana devnet.</T>
            <Button kind="secondary" title="Enable mainnet…" onPress={enableMainnet} />
          </>
        )}
        {s.mainnetEnabled ? <Button kind="secondary" title="Disable mainnet" onPress={() => void w.updateSettings({ mainnetEnabled: false })} /> : null}
      </Card>
      <Card>
        <T kind="h2">Sweep: ignore tokens worth less than</T>
        <Choice
          options={[0, 1, 5, 20].map((v) => ({ value: String(v), label: `$${v}` }))}
          value={String(s.dustUsd)}
          onChange={(v) => void w.updateSettings({ dustUsd: Number(v) })}
        />
      </Card>
      <Card>
        <T kind="h2">Privacy</T>
        <T kind="muted">This app stores only public data (account public keys and addresses) in the phone’s secure storage, excluded from backups. Balances are fetched from public APIs (mempool.space, Blockscout, public RPC nodes, CoinGecko, DexScreener, Jupiter), which can see which addresses you look up.</T>
        <Button
          kind="danger"
          title="Forget all accounts on this phone"
          onPress={() =>
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
            ])
          }
        />
      </Card>
    </Screen>
  );
}
