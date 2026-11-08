import { Tabs } from 'expo-router';
import type { BottomTabBarProps } from 'expo-router/tabs';
import { View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { ActionButton, ButtonBar, useTheme, type IconName } from '../../ui/components';

const TABS: { route: string; label: string; icon: IconName }[] = [
  { route: 'index', label: 'Wallet', icon: 'wallet-outline' },
  { route: 'activity', label: 'History', icon: 'time-outline' },
  { route: 'receive', label: 'Receive', icon: 'arrow-down-circle-outline' },
  { route: 'send', label: 'Send', icon: 'arrow-up-circle-outline' },
  { route: 'device', label: 'Device', icon: 'hardware-chip-outline' },
];

/** Electrum's bottom ButtonContainer (Receive | Send), extended into the app's tabs + Device. */
function TabBar({ state, navigateToTab }: BottomTabBarProps) {
  const t = useTheme();
  const insets = useSafeAreaInsets();
  const current = state.routes[state.index]?.name;
  const go = (name: string) => {
    const r = state.routes.find((x) => x.name === name);
    if (r) navigateToTab(r.key);
  };
  return (
    <View style={{ backgroundColor: t.card, paddingBottom: insets.bottom }}>
      <ButtonBar>
        {TABS.map((it) => (
          <ActionButton key={it.route} icon={it.icon} label={it.label} selected={current === it.route} onPress={() => go(it.route)} />
        ))}
      </ButtonBar>
    </View>
  );
}

export default function TabLayout() {
  const t = useTheme();
  return (
    <Tabs tabBar={(p) => <TabBar {...p} />} screenOptions={{ headerShown: false, sceneStyle: { backgroundColor: t.bg } }}>
      <Tabs.Screen name="index" options={{ title: 'Wallet' }} />
      <Tabs.Screen name="activity" options={{ title: 'History' }} />
      <Tabs.Screen name="receive" options={{ title: 'Receive' }} />
      <Tabs.Screen name="send" options={{ title: 'Send' }} />
      <Tabs.Screen name="device" options={{ title: 'Device' }} />
    </Tabs>
  );
}
