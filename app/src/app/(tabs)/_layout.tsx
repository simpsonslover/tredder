import Ionicons from '@expo/vector-icons/Ionicons';
import { Tabs } from 'expo-router';
import type { ComponentProps } from 'react';
import type { ColorValue } from 'react-native';

type IconName = ComponentProps<typeof Ionicons>['name'];
type TabIconProps = { color: ColorValue | null; size: number };

function icon(name: IconName) {
  function TabIcon({ color, size }: TabIconProps) {
    return <Ionicons name={name} color={typeof color === 'number' || color == null ? undefined : color} size={size} />;
  }
  return TabIcon;
}

export default function TabLayout() {
  return (
    <Tabs screenOptions={{ tabBarActiveTintColor: '#3b6cf6', headerShown: false }}>
      <Tabs.Screen name="index" options={{ title: 'Portfolio', tabBarIcon: icon('wallet-outline') }} />
      <Tabs.Screen name="receive" options={{ title: 'Receive', tabBarIcon: icon('arrow-down-circle-outline') }} />
      <Tabs.Screen name="send" options={{ title: 'Send', tabBarIcon: icon('arrow-up-circle-outline') }} />
      <Tabs.Screen name="activity" options={{ title: 'Activity', tabBarIcon: icon('time-outline') }} />
      <Tabs.Screen name="device" options={{ title: 'Device', tabBarIcon: icon('hardware-chip-outline') }} />
    </Tabs>
  );
}
