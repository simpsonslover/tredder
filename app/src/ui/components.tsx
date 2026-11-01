import type { ReactNode } from 'react';
import {
  ActivityIndicator,
  Modal,
  Pressable,
  RefreshControl,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
  useColorScheme,
  type TextInputProps,
  type ViewStyle,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

export function useTheme() {
  const dark = useColorScheme() === 'dark';
  return {
    dark,
    bg: dark ? '#0b0d12' : '#f4f5f8',
    card: dark ? '#161a22' : '#ffffff',
    text: dark ? '#eef0f4' : '#14161b',
    muted: dark ? '#8b93a3' : '#667085',
    border: dark ? '#262b36' : '#e4e7ec',
    accent: '#3b6cf6',
    danger: '#d92d20',
    warn: '#dc6803',
    ok: '#12b76a',
  };
}

export function Screen({ children, onRefresh, refreshing }: { children: ReactNode; onRefresh?: () => void; refreshing?: boolean }) {
  const t = useTheme();
  return (
    <SafeAreaView style={{ flex: 1, backgroundColor: t.bg }} edges={['top']}>
      <ScrollView
        contentContainerStyle={{ padding: 16, gap: 12, paddingBottom: 40 }}
        refreshControl={onRefresh ? <RefreshControl refreshing={!!refreshing} onRefresh={onRefresh} /> : undefined}
      >
        {children}
      </ScrollView>
    </SafeAreaView>
  );
}

export function Card({ children, style }: { children: ReactNode; style?: ViewStyle }) {
  const t = useTheme();
  return <View style={[{ backgroundColor: t.card, borderRadius: 14, padding: 14, gap: 8, borderWidth: 1, borderColor: t.border }, style]}>{children}</View>;
}

type TextKind = 'title' | 'h2' | 'body' | 'muted' | 'mono' | 'error' | 'warn' | 'ok';
export function T({ children, kind = 'body', center, selectable }: { children: ReactNode; kind?: TextKind; center?: boolean; selectable?: boolean }) {
  const t = useTheme();
  const s: Record<TextKind, object> = {
    title: { fontSize: 28, fontWeight: '700', color: t.text },
    h2: { fontSize: 17, fontWeight: '600', color: t.text },
    body: { fontSize: 15, color: t.text },
    muted: { fontSize: 13, color: t.muted },
    mono: { fontSize: 14, color: t.text, fontFamily: 'monospace' },
    error: { fontSize: 14, color: t.danger },
    warn: { fontSize: 14, color: t.warn },
    ok: { fontSize: 14, color: t.ok, fontWeight: '600' },
  };
  return (
    <Text selectable={selectable} style={[s[kind], center && { textAlign: 'center' }]}>
      {children}
    </Text>
  );
}

export function Button({ title, onPress, kind = 'primary', loading, disabled }: { title: string; onPress: () => void; kind?: 'primary' | 'secondary' | 'danger'; loading?: boolean; disabled?: boolean }) {
  const t = useTheme();
  const bg = kind === 'primary' ? t.accent : kind === 'danger' ? t.danger : 'transparent';
  const fg = kind === 'secondary' ? t.accent : '#fff';
  return (
    <Pressable
      accessibilityRole="button"
      onPress={onPress}
      disabled={disabled || loading}
      style={({ pressed }) => [
        { backgroundColor: bg, borderRadius: 12, paddingVertical: 13, alignItems: 'center', borderWidth: kind === 'secondary' ? 1 : 0, borderColor: t.accent },
        (pressed || disabled) && { opacity: 0.55 },
      ]}
    >
      {loading ? <ActivityIndicator color={fg} /> : <Text style={{ color: fg, fontWeight: '600', fontSize: 15 }}>{title}</Text>}
    </Pressable>
  );
}

export function Row({ left, right, sub, onPress }: { left: ReactNode; right?: ReactNode; sub?: ReactNode; onPress?: () => void }) {
  const t = useTheme();
  return (
    <Pressable onPress={onPress} disabled={!onPress} style={{ flexDirection: 'row', alignItems: 'center', paddingVertical: 8, gap: 8, borderBottomWidth: StyleSheet.hairlineWidth, borderColor: t.border }}>
      <View style={{ flex: 1, gap: 2 }}>
        {typeof left === 'string' ? <T>{left}</T> : left}
        {sub ? typeof sub === 'string' ? <T kind="muted">{sub}</T> : sub : null}
      </View>
      {right ? <View style={{ alignItems: 'flex-end' }}>{typeof right === 'string' ? <T>{right}</T> : right}</View> : null}
    </Pressable>
  );
}

export function Badge({ label, color }: { label: string; color?: string }) {
  const t = useTheme();
  return (
    <View style={{ alignSelf: 'flex-start', backgroundColor: (color ?? t.accent) + '22', borderRadius: 999, paddingHorizontal: 10, paddingVertical: 3 }}>
      <Text style={{ color: color ?? t.accent, fontSize: 12, fontWeight: '600' }}>{label}</Text>
    </View>
  );
}

export function Field(props: TextInputProps & { label: string }) {
  const t = useTheme();
  return (
    <View style={{ gap: 4 }}>
      <T kind="muted">{props.label}</T>
      <TextInput
        autoCapitalize="none"
        autoCorrect={false}
        placeholderTextColor={t.muted}
        {...props}
        style={{ borderWidth: 1, borderColor: t.border, borderRadius: 10, padding: 12, color: t.text, backgroundColor: t.card, fontSize: 15 }}
      />
    </View>
  );
}

export function Choice<V extends string>({ options, value, onChange }: { options: { value: V; label: string }[]; value: V; onChange: (v: V) => void }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 6 }}>
      {options.map((o) => (
        <Pressable
          key={o.value}
          onPress={() => onChange(o.value)}
          style={{ paddingHorizontal: 12, paddingVertical: 7, borderRadius: 999, borderWidth: 1, borderColor: o.value === value ? t.accent : t.border, backgroundColor: o.value === value ? t.accent + '18' : 'transparent' }}
        >
          <Text style={{ color: o.value === value ? t.accent : t.text, fontSize: 13 }}>{o.label}</Text>
        </Pressable>
      ))}
    </View>
  );
}

/** Shown whenever the device is waiting for a physical button press. */
export function ConfirmOnDevice({ visible, what }: { visible: boolean; what?: string }) {
  const t = useTheme();
  return (
    <Modal visible={visible} transparent animationType="fade">
      <View style={{ flex: 1, backgroundColor: '#000a', justifyContent: 'center', padding: 24 }}>
        <Card style={{ alignItems: 'center', gap: 12, paddingVertical: 28 }}>
          <ActivityIndicator size="large" color={t.accent} />
          <T kind="h2" center>Confirm on your device</T>
          <T kind="muted" center>{what ?? 'Check every detail on the device screen, then press Confirm. Press Back to cancel.'}</T>
        </Card>
      </View>
    </Modal>
  );
}
