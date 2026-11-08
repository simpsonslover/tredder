import { DarkTheme, Stack, ThemeProvider } from 'expo-router';
import { StatusBar } from 'expo-status-bar';

import { WalletProvider, useWallet } from '../state/WalletContext';
import { ConfirmOnDevice, useTheme } from '../ui/components';

function Overlay() {
  const { awaitingUser, awaitingKind, pairCode } = useWallet();
  // Receive draws its own prompt: the address must stay visible to compare with the device
  const pairing = awaitingKind === 'pair';
  return (
    <ConfirmOnDevice
      visible={awaitingUser && awaitingKind !== 'show-address'}
      code={pairing ? pairCode ?? '------' : undefined}
      what={pairing ? 'Approve on your tredder only if it shows exactly this code. If the codes differ, reject it: something is between your phone and the device.' : undefined}
    />
  );
}

export default function RootLayout() {
  const t = useTheme();
  const nav = { ...DarkTheme, colors: { ...DarkTheme.colors, background: t.bg, card: t.bg, text: t.text, border: t.border, primary: t.accent } };
  return (
    <ThemeProvider value={nav}>
      <WalletProvider>
        <StatusBar style="light" />
        <Stack
          screenOptions={{
            headerStyle: { backgroundColor: t.card2 },
            headerTintColor: t.text,
            headerShadowVisible: false,
            headerTitleStyle: { fontWeight: '700', fontSize: 16 },
            contentStyle: { backgroundColor: t.bg },
          }}
        >
          <Stack.Screen name="(tabs)" options={{ headerShown: false }} />
          <Stack.Screen name="import" options={{ title: 'Find accounts' }} />
          <Stack.Screen name="sweep" options={{ title: 'Sweep to new wallet' }} />
          <Stack.Screen name="settings" options={{ title: 'Preferences' }} />
        </Stack>
        <Overlay />
      </WalletProvider>
    </ThemeProvider>
  );
}
