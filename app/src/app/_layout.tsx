import { DarkTheme, Stack, ThemeProvider } from 'expo-router';
import { StatusBar } from 'expo-status-bar';

import { WalletProvider, useWallet } from '../state/WalletContext';
import { CrashGuard } from '../ui/CrashScreen';
import { ConfirmOnDevice, PairWithCode, useTheme } from '../ui/components';

function Overlay() {
  const { awaitingUser, awaitingKind, pairCode, pairing, disconnect } = useWallet();
  if (pairing) return <PairWithCode visible code={pairCode} onCancel={() => void disconnect()} />;
  // Receive draws its own prompt: the address must stay visible to compare with the device
  return <ConfirmOnDevice visible={awaitingUser && awaitingKind !== 'show-address'} />;
}

export default function RootLayout() {
  const t = useTheme();
  const nav = { ...DarkTheme, colors: { ...DarkTheme.colors, background: t.bg, card: t.bg, text: t.text, border: t.border, primary: t.accent } };
  return (
    <CrashGuard>
    <ThemeProvider value={nav}>
      <WalletProvider>
        <StatusBar style="light" />
        <Stack
          screenOptions={{
            /* the top stop of the page gradient, so the native header blends into the screen */
            headerStyle: { backgroundColor: t.bgGrad[0] },
            headerTintColor: t.text,
            headerShadowVisible: false,
            headerTitleStyle: { fontWeight: '700', fontSize: 18 },
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
    </CrashGuard>
  );
}
