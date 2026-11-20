import { Tabs } from 'expo-router';
import type { BottomTabBarProps } from 'expo-router/tabs';
import { View } from 'react-native';
import { useSafeAreaInsets } from 'react-native-safe-area-context';

import { GradientView } from '../../ui/gradient';
import { ActionButton, ButtonBar, shadow, useTheme, type IconName } from '../../ui/components';

const TABS: { route: string; label: string; icon: IconName }[] = [
  { route: 'index', label: 'Wallet', icon: 'wallet-outline' },
  { route: 'activity', label: 'History', icon: 'time-outline' },
  { route: 'receive', label: 'Receive', icon: 'arrow-down-circle-outline' },
  { route: 'send', label: 'Send', icon: 'arrow-up-circle-outline' },
  { route: 'device', label: 'Device', icon: 'cube-outline' },
];

/** Floating rounded tab bar, lifted clear of the home indicator. */
function TabBar({ state, navigateToTab }: BottomTabBarProps) {
  const t = useTheme();
  const insets = useSafeAreaInsets();
  const current = state.routes[state.index]?.name;
  const go = (name: string) => {
    const r = state.routes.find((x) => x.name === name);
    if (r) navigateToTab(r.key);
  };
  return (
    <View style={{ paddingHorizontal: 12, paddingBottom: Math.max(insets.bottom, 10), paddingTop: 6, backgroundColor: 'transparent' }}>
      <GradientView colors={t.card2Grad} radius={t.r.xl} border={t.lineStrong} style={[{ paddingHorizontal: 6, paddingVertical: 4 }, shadow(3)]}>
        <ButtonBar>
          {TABS.map((it) => (
            <ActionButton key={it.route} icon={it.icon} label={it.label} selected={current === it.route} onPress={() => go(it.route)} />
          ))}
        </ButtonBar>
      </GradientView>
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
