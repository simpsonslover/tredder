import Ionicons from '@expo/vector-icons/Ionicons';
import { Tabs, router } from 'expo-router';
import type { BottomTabBarProps } from 'expo-router/tabs';
import { useState } from 'react';
import { Pressable, Text, View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { Sheet, SheetItem, useTheme, type IconName } from '../../ui/components';

type Item = { route?: string; push?: '/settings'; label: string; icon: IconName; iconOn: IconName };
const LEFT: Item[] = [
  { route: 'index', label: 'Wallet', icon: 'wallet-outline', iconOn: 'wallet' },
  { route: 'activity', label: 'Activity', icon: 'time-outline', iconOn: 'time' },
];
const RIGHT: Item[] = [
  { route: 'device', label: 'Device', icon: 'hardware-chip-outline', iconOn: 'hardware-chip' },
  { push: '/settings', label: 'Settings', icon: 'settings-outline', iconOn: 'settings' },
];

/** Bottom bar: 4 tabs around a round Transfer button that opens Send / Receive / Sweep. */
function TabBar({ state, navigateToTab }: BottomTabBarProps) {
  const t = useTheme();
  const insets = useSafeAreaInsets();
  const [transfer, setTransfer] = useState(false);
  const current = state.routes[state.index]?.name;
  const navigate = (name: string) => {
    const r = state.routes.find((x) => x.name === name);
    if (r) navigateToTab(r.key);
  };

  const tab = (it: Item) => {
    const on = it.route === current;
    return (
      <Pressable
        key={it.label}
        accessibilityRole="tab"
        accessibilityState={{ selected: on }}
        onPress={() => (it.push ? router.push(it.push) : navigate(it.route!))}
        style={{ flex: 1, alignItems: 'center', gap: 3, paddingTop: 10 }}
      >
        <Ionicons name={on ? it.iconOn : it.icon} size={22} color={on ? t.text : t.muted} />
        <Text style={{ fontSize: 11, color: on ? t.text : t.muted, fontWeight: on ? '600' : '400' }}>{it.label}</Text>
      </Pressable>
    );
  };

  const go = (route: 'send' | 'receive' | '/sweep') => {
    setTransfer(false);
    if (route === '/sweep') router.push('/sweep');
    else navigate(route);
  };

  return (
    <View style={{ flexDirection: 'row', backgroundColor: t.bg, borderTopWidth: 1, borderTopColor: t.border, paddingBottom: Math.max(insets.bottom, 10) }}>
      {LEFT.map(tab)}
      <View style={{ flex: 1, alignItems: 'center' }}>
        <Pressable
          accessibilityRole="button"
          accessibilityLabel="Transfer"
          onPress={() => setTransfer(true)}
          style={({ pressed }) => [
            { width: 56, height: 56, borderRadius: 28, backgroundColor: t.accent, alignItems: 'center', justifyContent: 'center', marginTop: -18 },
            pressed && { opacity: 0.8 },
          ]}
        >
          <Ionicons name="swap-vertical" size={26} color={t.onAccent} />
        </Pressable>
      </View>
      {RIGHT.map(tab)}
      <Sheet visible={transfer} onClose={() => setTransfer(false)} title="Transfer">
        <SheetItem icon="arrow-up" title="Send" sub="Send crypto, signed on your device" onPress={() => go('send')} />
        <SheetItem icon="arrow-down" title="Receive" sub="Get an address verified on your device" onPress={() => go('receive')} />
        <SheetItem icon="shuffle" title="Sweep" sub="Move an imported seed's funds to your device wallet" onPress={() => go('/sweep')} />
      </Sheet>
    </View>
  );
}

export default function TabLayout() {
  const t = useTheme();
  return (
    <Tabs tabBar={(p) => <TabBar {...p} />} screenOptions={{ headerShown: false, sceneStyle: { backgroundColor: t.bg } }}>
      <Tabs.Screen name="index" options={{ title: 'Wallet' }} />
      <Tabs.Screen name="activity" options={{ title: 'Activity' }} />
      <Tabs.Screen name="send" options={{ title: 'Send' }} />
      <Tabs.Screen name="receive" options={{ title: 'Receive' }} />
      <Tabs.Screen name="device" options={{ title: 'Device' }} />
    </Tabs>
  );
}
