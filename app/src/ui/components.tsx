import Ionicons from '@expo/vector-icons/Ionicons';
import type { ComponentProps, ReactNode } from 'react';
import {
  ActivityIndicator,
  Modal,
  Pressable,
  RefreshControl,
  ScrollView,
  Text,
  TextInput,
  View,
  type TextInputProps,
  type ViewStyle,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

/* One dark theme: near-black background, raised grey surfaces, soft purple accent, pill buttons. */
const THEME = {
  dark: true,
  bg: '#0B0B0D',
  card: '#17171A',
  card2: '#222226',
  text: '#FFFFFF',
  muted: '#9A9AA3',
  border: '#2A2A2F',
  accent: '#A48BFF',
  onAccent: '#0B0B0D',
  danger: '#FF5C61',
  warn: '#FFB547',
  ok: '#5DD39E',
};
export type Theme = typeof THEME;
export function useTheme(): Theme {
  return THEME;
}

export type IconName = ComponentProps<typeof Ionicons>['name'];

export function Screen({ children, onRefresh, refreshing }: { children: ReactNode; onRefresh?: () => void; refreshing?: boolean }) {
  const t = useTheme();
  return (
    <SafeAreaView style={{ flex: 1, backgroundColor: t.bg }} edges={['top']}>
      <ScrollView
        contentContainerStyle={{ paddingHorizontal: 20, paddingTop: 12, gap: 14, paddingBottom: 48 }}
        refreshControl={onRefresh ? <RefreshControl tintColor={t.muted} refreshing={!!refreshing} onRefresh={onRefresh} /> : undefined}
      >
        {children}
      </ScrollView>
    </SafeAreaView>
  );
}

export function Card({ children, style }: { children: ReactNode; style?: ViewStyle }) {
  const t = useTheme();
  return <View style={[{ backgroundColor: t.card, borderRadius: 16, padding: 16, gap: 10 }, style]}>{children}</View>;
}

type TextKind = 'hero' | 'title' | 'h2' | 'body' | 'muted' | 'mono' | 'error' | 'warn' | 'ok' | 'section';
export function T({ children, kind = 'body', center, selectable }: { children: ReactNode; kind?: TextKind; center?: boolean; selectable?: boolean }) {
  const t = useTheme();
  const s: Record<TextKind, object> = {
    hero: { fontSize: 40, fontWeight: '700', color: t.text, letterSpacing: -0.5 },
    title: { fontSize: 28, fontWeight: '700', color: t.text },
    h2: { fontSize: 18, fontWeight: '600', color: t.text },
    section: { fontSize: 13, fontWeight: '600', color: t.muted, textTransform: 'uppercase', letterSpacing: 0.8 },
    body: { fontSize: 15, color: t.text },
    muted: { fontSize: 13, color: t.muted, lineHeight: 18 },
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

export function Button({ title, onPress, kind = 'primary', loading, disabled, icon }: { title: string; onPress: () => void; kind?: 'primary' | 'secondary' | 'danger'; loading?: boolean; disabled?: boolean; icon?: IconName }) {
  const t = useTheme();
  const bg = kind === 'primary' ? t.accent : kind === 'danger' ? t.danger : t.card2;
  const fg = kind === 'primary' ? t.onAccent : '#fff';
  return (
    <Pressable
      accessibilityRole="button"
      onPress={onPress}
      disabled={disabled || loading}
      style={({ pressed }) => [
        { backgroundColor: bg, borderRadius: 999, paddingVertical: 14, paddingHorizontal: 20, alignItems: 'center', justifyContent: 'center', flexDirection: 'row', gap: 8 },
        (pressed || disabled) && { opacity: 0.5 },
      ]}
    >
      {loading ? (
        <ActivityIndicator color={fg} />
      ) : (
        <>
          {icon ? <Ionicons name={icon} size={18} color={fg} /> : null}
          <Text style={{ color: fg, fontWeight: '600', fontSize: 15 }}>{title}</Text>
        </>
      )}
    </Pressable>
  );
}

export function Row({ left, right, sub, onPress, icon }: { left: ReactNode; right?: ReactNode; sub?: ReactNode; onPress?: () => void; icon?: ReactNode }) {
  return (
    <Pressable onPress={onPress} disabled={!onPress} style={({ pressed }) => [{ flexDirection: 'row', alignItems: 'center', paddingVertical: 10, gap: 12 }, pressed && { opacity: 0.6 }]}>
      {icon}
      <View style={{ flex: 1, gap: 2 }}>
        {typeof left === 'string' ? <T>{left}</T> : left}
        {sub ? typeof sub === 'string' ? <T kind="muted">{sub}</T> : sub : null}
      </View>
      {right ? <View style={{ alignItems: 'flex-end', gap: 2 }}>{typeof right === 'string' ? <T>{right}</T> : right}</View> : null}
    </Pressable>
  );
}

export function Badge({ label, color }: { label: string; color?: string }) {
  const t = useTheme();
  return (
    <View style={{ alignSelf: 'flex-start', backgroundColor: (color ?? t.accent) + '26', borderRadius: 999, paddingHorizontal: 10, paddingVertical: 4 }}>
      <Text style={{ color: color ?? t.accent, fontSize: 11, fontWeight: '700', letterSpacing: 0.5 }}>{label}</Text>
    </View>
  );
}

export function Field(props: TextInputProps & { label: string }) {
  const t = useTheme();
  return (
    <View style={{ gap: 6 }}>
      <T kind="muted">{props.label}</T>
      <TextInput
        autoCapitalize="none"
        autoCorrect={false}
        placeholderTextColor={t.muted}
        {...props}
        style={{ borderRadius: 12, paddingHorizontal: 14, paddingVertical: 13, color: t.text, backgroundColor: t.card2, fontSize: 15 }}
      />
    </View>
  );
}

export function Choice<V extends string>({ options, value, onChange }: { options: { value: V; label: string }[]; value: V; onChange: (v: V) => void }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 8 }}>
      {options.map((o) => {
        const on = o.value === value;
        return (
          <Pressable
            key={o.value}
            onPress={() => onChange(o.value)}
            style={{ paddingHorizontal: 14, paddingVertical: 8, borderRadius: 999, backgroundColor: on ? t.text : t.card2 }}
          >
            <Text style={{ color: on ? t.bg : t.text, fontSize: 13, fontWeight: on ? '600' : '400' }}>{o.label}</Text>
          </Pressable>
        );
      })}
    </View>
  );
}

/** Two-way switch styled as a segmented pill (e.g. Assets | Accounts). */
export function Segmented<V extends string>({ options, value, onChange }: { options: { value: V; label: string }[]; value: V; onChange: (v: V) => void }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', backgroundColor: t.card, borderRadius: 999, padding: 4 }}>
      {options.map((o) => {
        const on = o.value === value;
        return (
          <Pressable key={o.value} onPress={() => onChange(o.value)} style={{ flex: 1, alignItems: 'center', paddingVertical: 8, borderRadius: 999, backgroundColor: on ? t.card2 : 'transparent' }}>
            <Text style={{ color: on ? t.text : t.muted, fontWeight: '600', fontSize: 14 }}>{o.label}</Text>
          </Pressable>
        );
      })}
    </View>
  );
}

/** Round icon button with a label underneath (portfolio quick actions). */
export function ActionButton({ icon, label, onPress, disabled }: { icon: IconName; label: string; onPress: () => void; disabled?: boolean }) {
  const t = useTheme();
  return (
    <Pressable onPress={onPress} disabled={disabled} style={({ pressed }) => [{ alignItems: 'center', gap: 8, flex: 1 }, (pressed || disabled) && { opacity: 0.5 }]}>
      <View style={{ width: 52, height: 52, borderRadius: 26, backgroundColor: t.card2, alignItems: 'center', justifyContent: 'center' }}>
        <Ionicons name={icon} size={22} color={t.text} />
      </View>
      <Text style={{ color: t.text, fontSize: 13, fontWeight: '500' }}>{label}</Text>
    </Pressable>
  );
}

/* Plain monogram badges (not project logos): a colour per asset, letters on top. */
const COIN_COLORS: Record<string, string> = {
  BTC: '#F7931A',
  ETH: '#7B8CDE',
  SOL: '#14C9A0',
  BNB: '#E8B30B',
  POL: '#8247E5',
  USDC: '#2775CA',
  USDT: '#26A17B',
  DAI: '#E9A23B',
};
export function coinColor(symbol: string): string {
  const known = COIN_COLORS[symbol.toUpperCase()];
  if (known) return known;
  /* stable hex colour from the symbol (hex, so alpha suffixes like + '33' work) */
  let h = 0;
  for (const c of symbol) h = (h * 31 + c.charCodeAt(0)) % 360;
  const f = (n: number) => {
    const k = (n + h / 30) % 12;
    const l = 0.6, a = 0.6 * Math.min(l, 1 - l); /* HSL(h, 60%, 60%) */
    const v = l - a * Math.max(-1, Math.min(k - 3, 9 - k, 1));
    return Math.round(v * 255).toString(16).padStart(2, '0');
  };
  return `#${f(0)}${f(8)}${f(4)}`;
}

export function CoinIcon({ symbol, size = 40, small }: { symbol: string; size?: number; small?: string }) {
  const t = useTheme();
  const c = coinColor(symbol);
  return (
    <View style={{ width: size, height: size }}>
      <View style={{ width: size, height: size, borderRadius: size / 2, backgroundColor: c + '33', alignItems: 'center', justifyContent: 'center' }}>
        <Text style={{ color: c, fontWeight: '800', fontSize: size * (symbol.length > 3 ? 0.26 : 0.32) }}>{symbol.slice(0, 4).toUpperCase()}</Text>
      </View>
      {small ? (
        <View style={{ position: 'absolute', right: -3, bottom: -3, backgroundColor: t.bg, borderRadius: 9, padding: 2 }}>
          <View style={{ width: 14, height: 14, borderRadius: 7, backgroundColor: coinColor(small), alignItems: 'center', justifyContent: 'center' }}>
            <Text style={{ color: '#fff', fontSize: 7, fontWeight: '800' }}>{small.slice(0, 1)}</Text>
          </View>
        </View>
      ) : null}
    </View>
  );
}

/** Horizontal bar split by share of total value. */
export function AllocationBar({ parts }: { parts: { key: string; value: number; color: string }[] }) {
  const t = useTheme();
  const total = parts.reduce((s, p) => s + p.value, 0);
  if (!(total > 0)) return null;
  return (
    <View style={{ flexDirection: 'row', height: 8, borderRadius: 4, overflow: 'hidden', backgroundColor: t.card2, gap: 2 }}>
      {parts.filter((p) => p.value > 0).map((p) => (
        <View key={p.key} style={{ flex: p.value / total, backgroundColor: p.color }} />
      ))}
    </View>
  );
}

/** Bottom sheet with large rows (transfer menu). */
export function Sheet({ visible, onClose, title, children }: { visible: boolean; onClose: () => void; title: string; children: ReactNode }) {
  const t = useTheme();
  return (
    <Modal visible={visible} transparent animationType="slide" onRequestClose={onClose}>
      <Pressable style={{ flex: 1, backgroundColor: '#000a' }} onPress={onClose} />
      <View style={{ backgroundColor: t.card, borderTopLeftRadius: 24, borderTopRightRadius: 24, padding: 20, paddingBottom: 40, gap: 6 }}>
        <View style={{ alignSelf: 'center', width: 40, height: 4, borderRadius: 2, backgroundColor: t.border, marginBottom: 10 }} />
        <T kind="h2">{title}</T>
        {children}
      </View>
    </Modal>
  );
}

export function SheetItem({ icon, title, sub, onPress }: { icon: IconName; title: string; sub: string; onPress: () => void }) {
  const t = useTheme();
  return (
    <Row
      onPress={onPress}
      icon={
        <View style={{ width: 44, height: 44, borderRadius: 22, backgroundColor: t.card2, alignItems: 'center', justifyContent: 'center' }}>
          <Ionicons name={icon} size={20} color={t.text} />
        </View>
      }
      left={<T kind="h2">{title}</T>}
      sub={sub}
      right={<Ionicons name="chevron-forward" size={18} color={t.muted} />}
    />
  );
}

/** Shown whenever the device is waiting for a physical button press. */
export function ConfirmOnDevice({ visible, what }: { visible: boolean; what?: string }) {
  const t = useTheme();
  return (
    <Modal visible={visible} transparent animationType="fade">
      <View style={{ flex: 1, backgroundColor: '#000c', justifyContent: 'center', padding: 24 }}>
        <Card style={{ alignItems: 'center', gap: 14, paddingVertical: 32 }}>
          <View style={{ width: 64, height: 64, borderRadius: 32, backgroundColor: t.accent + '26', alignItems: 'center', justifyContent: 'center' }}>
            <Ionicons name="hardware-chip-outline" size={30} color={t.accent} />
          </View>
          <T kind="h2" center>Confirm on your device</T>
          <T kind="muted" center>{what ?? 'Check every detail on the device screen, then press Confirm. Press Back to cancel.'}</T>
          <ActivityIndicator color={t.accent} />
        </Card>
      </View>
    </Modal>
  );
}
