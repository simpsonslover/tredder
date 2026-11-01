import { Stack } from 'expo-router';
import { StatusBar } from 'expo-status-bar';

import { WalletProvider, useWallet } from '../state/WalletContext';
import { ConfirmOnDevice } from '../ui/components';

function Overlay() {
  const { awaitingUser } = useWallet();
  return <ConfirmOnDevice visible={awaitingUser} />;
}

export default function RootLayout() {
  return (
    <WalletProvider>
      <StatusBar style="auto" />
      <Stack>
        <Stack.Screen name="(tabs)" options={{ headerShown: false }} />
        <Stack.Screen name="import" options={{ title: 'Find accounts' }} />
        <Stack.Screen name="sweep" options={{ title: 'Sweep to new wallet' }} />
        <Stack.Screen name="settings" options={{ title: 'Settings' }} />
      </Stack>
      <Overlay />
    </WalletProvider>
  );
}
