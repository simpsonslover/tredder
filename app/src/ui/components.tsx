/*
 * UI kit in the style of Electrum's mobile app (electrum/gui/qml, MIT licence): Qt Material Dark
 * (#303030 background, #424242 toolbars and dialogs, light-blue accent), flat toolbars, rounded
 * "highlight panes", monospace amounts in green / red, accent-coloured section labels, and flat
 * icon-over-text button bars with thin separators. Values below are taken from Electrum's
 * Constants.qml / main.qml; the icons are Ionicons, not Electrum's PNGs.
 */
import Ionicons from '@expo/vector-icons/Ionicons';
import type { ComponentProps, ReactNode } from 'react';
import {
  ActivityIndicator,
  Image,
  Modal,
  Platform,
  Pressable,
  RefreshControl,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
  type ImageSourcePropType,
  type TextInputProps,
  type ViewStyle,
} from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import { Identicon } from './Identicon';
import { useDownloadedLogo } from './logoStore';

const THEME = {
  dark: true,
  bg: '#303030', //          Material.background (Dark)
  card: '#3E3E3E', //        constants.highlightBackground = lighter(background, 1.3)
  card2: '#424242', //       Material.dialogColor (toolbar, dialogs)
  darker: '#282828', //      constants.darkerBackground = darker(background, 1.2)
  text: '#FFFFFF',
  muted: '#9E9E9E', //       constants.mutedForeground ('gray', a little lighter for legibility)
  border: '#282828',
  accent: '#81D4FA', //      Material.LightBlue accent, dark theme
  accent2: '#81D4FA',
  onAccent: '#000000',
  danger: '#FF8080', //      colorError / colorDebit
  warn: '#FFEE58', //        colorWarning ('yellow', softened)
  ok: '#80FF80', //          colorCredit / colorDone
};
export type Theme = typeof THEME;
export function useTheme(): Theme {
  return THEME;
}

export const MONO = Platform.select({ ios: 'Menlo', default: 'monospace' });

export type IconName = ComponentProps<typeof Ionicons>['name'];

/** Electrum's ToolBar: #424242, 48 high, small icon + bold title on the left, status on the right. */
export function Toolbar({ title, icon, right, onBack }: { title: string; icon?: IconName; right?: ReactNode; onBack?: () => void }) {
  const t = useTheme();
  return (
    <View
      style={{
        height: 52,
        backgroundColor: t.card2,
        flexDirection: 'row',
        alignItems: 'center',
        paddingLeft: onBack ? 6 : 20,
        paddingRight: 12,
        gap: 8,
        elevation: 4,
        shadowColor: '#000',
        shadowOpacity: 0.35,
        shadowRadius: 4,
        shadowOffset: { width: 0, height: 2 },
        zIndex: 2,
      }}
    >
      {onBack ? (
        <Pressable accessibilityRole="button" accessibilityLabel="Back" onPress={onBack} hitSlop={12} style={{ padding: 6 }}>
          <Ionicons name="arrow-back" size={22} color={t.text} />
        </Pressable>
      ) : icon ? (
        <Ionicons name={icon} size={16} color={t.text} />
      ) : null}
      <Text numberOfLines={1} style={{ flex: 1, color: t.text, fontSize: 16, fontWeight: '700' }}>
        {title}
      </Text>
      {right}
    </View>
  );
}

/** Small toolbar button (menu, settings). */
export function ToolbarButton({ icon, onPress, label }: { icon: IconName; onPress: () => void; label: string }) {
  const t = useTheme();
  return (
    <Pressable accessibilityRole="button" accessibilityLabel={label} onPress={onPress} hitSlop={10} style={({ pressed }) => [{ padding: 8 }, pressed && { opacity: 0.5 }]}>
      <Ionicons name={icon} size={22} color={t.text} />
    </Pressable>
  );
}

/** Network name under an info icon, as in Electrum's toolbar on testnet. */
export function NetworkTag({ label }: { label: string }) {
  const t = useTheme();
  return (
    <View style={{ alignItems: 'center', paddingHorizontal: 6 }}>
      <Ionicons name="information-circle" size={15} color={t.text} />
      <Text style={{ color: t.accent, fontSize: 10 }}>{label}</Text>
    </View>
  );
}

export function Screen({ children, onRefresh, refreshing, title, icon, right, footer }: { children: ReactNode; onRefresh?: () => void; refreshing?: boolean; title?: string; icon?: IconName; right?: ReactNode; footer?: ReactNode }) {
  const t = useTheme();
  return (
    <SafeAreaView style={{ flex: 1, backgroundColor: title ? t.card2 : t.bg }} edges={title ? ['top'] : []}>
      {title ? <Toolbar title={title} icon={icon} right={right} /> : null}
      <ScrollView
        style={{ backgroundColor: t.bg }}
        contentContainerStyle={{ paddingHorizontal: 12, paddingTop: 12, gap: 12, paddingBottom: 32 }}
        refreshControl={onRefresh ? <RefreshControl tintColor={t.muted} colors={[t.accent]} progressBackgroundColor={t.card2} refreshing={!!refreshing} onRefresh={onRefresh} /> : undefined}
      >
        {children}
      </ScrollView>
      {footer}
    </SafeAreaView>
  );
}

/** Electrum's TextHighlightPane: lighter background, radius 8. */
export function Card({ children, style }: { children: ReactNode; style?: ViewStyle }) {
  const t = useTheme();
  return <View style={[{ backgroundColor: t.card, borderRadius: 8, padding: 14, gap: 10 }, style]}>{children}</View>;
}
export const Pane = Card;

type TextKind = 'hero' | 'title' | 'h2' | 'body' | 'muted' | 'mono' | 'error' | 'warn' | 'ok' | 'section';
export function T({ children, kind = 'body', center, selectable, color }: { children: ReactNode; kind?: TextKind; center?: boolean; selectable?: boolean; color?: string }) {
  const t = useTheme();
  const s: Record<TextKind, object> = {
    hero: { fontSize: 28, color: t.text, fontFamily: MONO },
    title: { fontSize: 22, color: t.text },
    h2: { fontSize: 16, color: t.text },
    section: { fontSize: 15, color: t.accent, marginTop: 6 },
    body: { fontSize: 15, color: t.text, lineHeight: 21 },
    muted: { fontSize: 12, color: t.muted, lineHeight: 17 },
    mono: { fontSize: 14, color: t.text, fontFamily: MONO },
    error: { fontSize: 14, color: t.danger, lineHeight: 20 },
    warn: { fontSize: 14, color: t.warn, lineHeight: 20 },
    ok: { fontSize: 14, color: t.ok, fontWeight: '700' },
  };
  return (
    <Text selectable={selectable} style={[s[kind], center && { textAlign: 'center' }, color ? { color } : null]}>
      {children}
    </Text>
  );
}

/** Electrum's Heading: muted label between two hairlines. */
export function Heading({ text }: { text: string }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 12, marginVertical: 6 }}>
      <View style={{ flex: 1, height: 1, backgroundColor: t.muted + '80' }} />
      <Text style={{ color: t.muted, fontSize: 15 }}>{text}</Text>
      <View style={{ flex: 1, height: 1, backgroundColor: t.muted + '80' }} />
    </View>
  );
}

/** Flat button on the highlight colour: primary has accent text, as Electrum's dialog buttons. */
export function Button({ title, onPress, kind = 'primary', loading, disabled, icon }: { title: string; onPress: () => void; kind?: 'primary' | 'secondary' | 'danger'; loading?: boolean; disabled?: boolean; icon?: IconName }) {
  const t = useTheme();
  const fg = kind === 'primary' ? t.accent : kind === 'danger' ? t.danger : t.text;
  return (
    <Pressable
      accessibilityRole="button"
      onPress={onPress}
      disabled={disabled || loading}
      style={({ pressed }) => [
        {
          minHeight: 48,
          borderRadius: 8,
          paddingHorizontal: 16,
          alignItems: 'center',
          justifyContent: 'center',
          flexDirection: 'row',
          gap: 8,
          backgroundColor: pressed ? '#4E4E4E' : t.card,
        },
        kind === 'primary' && { borderWidth: 1, borderColor: t.accent + '66' },
        disabled && { opacity: 0.45 },
      ]}
    >
      {loading ? (
        <ActivityIndicator color={fg} />
      ) : (
        <>
          {icon ? <Ionicons name={icon} size={20} color={fg} /> : null}
          <Text style={{ color: fg, fontWeight: '600', fontSize: 15 }}>{title}</Text>
        </>
      )}
    </Pressable>
  );
}

export function Row({ left, right, sub, onPress, icon }: { left: ReactNode; right?: ReactNode; sub?: ReactNode; onPress?: () => void; icon?: ReactNode }) {
  return (
    <Pressable onPress={onPress} disabled={!onPress} style={({ pressed }) => [{ flexDirection: 'row', alignItems: 'center', paddingVertical: 10, paddingHorizontal: 4, gap: 12 }, pressed && { backgroundColor: '#ffffff0D' }]}>
      {icon}
      <View style={{ flex: 1, gap: 2 }}>
        {typeof left === 'string' ? <T kind="h2">{left}</T> : left}
        {sub ? typeof sub === 'string' ? <T kind="muted">{sub}</T> : sub : null}
      </View>
      {right ? <View style={{ alignItems: 'flex-end', gap: 2 }}>{typeof right === 'string' ? <T>{right}</T> : right}</View> : null}
    </Pressable>
  );
}

/** Plain 32 px icon (Electrum list rows use bare icons, no tile). */
export function IconTile({ icon, color, size = 32 }: { icon: IconName; color?: string; size?: number }) {
  const t = useTheme();
  return (
    <View style={{ width: size, height: size, alignItems: 'center', justifyContent: 'center' }}>
      <Ionicons name={icon} size={size * 0.75} color={color ?? t.text} />
    </View>
  );
}

/** Accent section label + rows separated by darker hairlines (Electrum's ListView sections). */
export function ListGroup({ title, children, footer }: { title?: string; children: ReactNode; footer?: string }) {
  const t = useTheme();
  const items = (Array.isArray(children) ? children.flat() : [children]).filter(Boolean);
  return (
    <View>
      {title ? <T kind="section">{title}</T> : null}
      <View>
        {items.map((c, i) => (
          <View key={i} style={i ? { borderTopWidth: 1, borderTopColor: t.darker } : undefined}>
            {c}
          </View>
        ))}
      </View>
      {footer ? <View style={{ paddingTop: 6, paddingHorizontal: 4 }}><T kind="muted">{footer}</T></View> : null}
    </View>
  );
}

/** Settings-style row: bare icon, title + muted subtitle, value or chevron. */
export function ListItem({ icon, color, title, sub, value, onPress, danger, chevron }: { icon: IconName; color?: string; title: string; sub?: string; value?: ReactNode; onPress?: () => void; danger?: boolean; chevron?: boolean }) {
  const t = useTheme();
  const showChevron = chevron ?? !!onPress;
  return (
    <Pressable onPress={onPress} disabled={!onPress} style={({ pressed }) => [{ flexDirection: 'row', alignItems: 'center', paddingVertical: 12, paddingHorizontal: 4, gap: 14 }, pressed && { backgroundColor: '#ffffff0D' }]}>
      <Ionicons name={icon} size={22} color={danger ? t.danger : color ?? t.muted} />
      <View style={{ flex: 1, gap: 2 }}>
        <Text style={{ color: danger ? t.danger : t.text, fontSize: 15 }}>{title}</Text>
        {sub ? <T kind="muted">{sub}</T> : null}
      </View>
      {typeof value === 'string' ? <Text style={{ color: t.text, fontSize: 14, fontFamily: MONO }}>{value}</Text> : value}
      {showChevron ? <Ionicons name="chevron-forward" size={18} color={t.muted} /> : null}
    </Pressable>
  );
}

/** "Label:" in accent, value on the right (Electrum's detail grids, e.g. "Balance: 0.001 BTC"). */
export function KeyValue({ label, value, unit, mono = true }: { label: string; value: ReactNode; unit?: string; mono?: boolean }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'baseline', gap: 8 }}>
      <Text style={{ color: t.accent, fontSize: 15, minWidth: 96 }}>{label}:</Text>
      <View style={{ flex: 1 }}>{typeof value === 'string' ? <Text style={{ color: t.text, fontSize: 15, fontFamily: mono ? MONO : undefined }}>{value}</Text> : value}</View>
      {unit ? <Text style={{ color: t.accent, fontSize: 15 }}>{unit}</Text> : null}
    </View>
  );
}

/** Coloured dot + label (connection state). */
export function StatusDot({ color, label }: { color: string; label: string; light?: boolean }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', alignItems: 'center', gap: 6, alignSelf: 'flex-start' }}>
      <View style={{ width: 9, height: 9, borderRadius: 5, backgroundColor: color }} />
      <Text style={{ color: t.text, fontSize: 14 }}>{label}</Text>
    </View>
  );
}

/** Electrum's InfoBanner-ish notice: icon + text on the highlight colour with a coloured edge. */
export function Notice({ kind = 'warn', icon, children }: { kind?: 'warn' | 'error' | 'info'; icon?: IconName; children: ReactNode }) {
  const t = useTheme();
  const c = kind === 'error' ? t.danger : kind === 'info' ? t.accent : t.warn;
  return (
    <View style={{ flexDirection: 'row', gap: 10, padding: 12, borderRadius: 8, backgroundColor: t.card, borderLeftWidth: 3, borderLeftColor: c }}>
      <Ionicons name={icon ?? (kind === 'error' ? 'alert-circle' : kind === 'info' ? 'information-circle' : 'warning')} size={20} color={c} />
      <View style={{ flex: 1, gap: 4 }}>{typeof children === 'string' ? <T>{children}</T> : children}</View>
    </View>
  );
}

export function Badge({ label, color }: { label: string; color?: string }) {
  const t = useTheme();
  const c = color ?? t.accent;
  return (
    <View style={{ alignSelf: 'flex-start', borderWidth: 1, borderColor: c, borderRadius: 4, paddingHorizontal: 6, paddingVertical: 2 }}>
      <Text style={{ color: c, fontSize: 11 }}>{label}</Text>
    </View>
  );
}

export function Field(props: TextInputProps & { label: string }) {
  const t = useTheme();
  return (
    <View style={{ gap: 4 }}>
      <Text style={{ color: t.accent, fontSize: 13 }}>{props.label}</Text>
      <TextInput
        autoCapitalize="none"
        autoCorrect={false}
        placeholderTextColor={t.muted}
        selectionColor={t.accent}
        {...props}
        style={{ paddingHorizontal: 4, paddingVertical: 10, color: t.text, fontSize: 15, borderBottomWidth: 2, borderBottomColor: t.accent }}
      />
    </View>
  );
}

/** Material radio buttons in a row. */
export function Choice<V extends string>({ options, value, onChange }: { options: { value: V; label: string }[]; value: V; onChange: (v: V) => void }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', flexWrap: 'wrap', gap: 4 }}>
      {options.map((o) => {
        const on = o.value === value;
        return (
          <Pressable key={o.value} onPress={() => onChange(o.value)} style={{ flexDirection: 'row', alignItems: 'center', gap: 8, paddingVertical: 8, paddingRight: 14 }}>
            <Ionicons name={on ? 'radio-button-on' : 'radio-button-off'} size={22} color={on ? t.accent : t.muted} />
            <Text style={{ color: t.text, fontSize: 15 }}>{o.label}</Text>
          </Pressable>
        );
      })}
    </View>
  );
}

/** Material TabBar: labels, accent underline on the selected one. */
export function Segmented<V extends string>({ options, value, onChange }: { options: { value: V; label: string }[]; value: V; onChange: (v: V) => void }) {
  const t = useTheme();
  return (
    <View style={{ flexDirection: 'row', borderBottomWidth: 1, borderBottomColor: t.darker }}>
      {options.map((o) => {
        const on = o.value === value;
        return (
          <Pressable key={o.value} onPress={() => onChange(o.value)} style={{ flex: 1, alignItems: 'center', paddingVertical: 12, borderBottomWidth: 2, borderBottomColor: on ? t.accent : 'transparent', marginBottom: -1 }}>
            <Text style={{ color: on ? t.accent : t.muted, fontWeight: '600', fontSize: 14 }}>{o.label}</Text>
          </Pressable>
        );
      })}
    </View>
  );
}

/** Electrum's FlatButton: icon over a small label; accent while pressed or selected. */
export function ActionButton({ icon, label, onPress, disabled, selected }: { icon: IconName; label: string; onPress: () => void; disabled?: boolean; selected?: boolean; onGradient?: boolean }) {
  const t = useTheme();
  return (
    <Pressable onPress={onPress} disabled={disabled} accessibilityRole="button" accessibilityState={{ selected }} style={{ flex: 1 }}>
      {({ pressed }) => {
        const c = disabled ? t.muted : pressed || selected ? t.accent : t.text;
        return (
          <View style={{ alignItems: 'center', gap: 4, paddingVertical: 9 }}>
            {selected ? <View style={{ position: 'absolute', top: 0, left: 8, right: 8, height: 3, backgroundColor: t.accent }} /> : null}
            <Ionicons name={icon} size={24} color={c} />
            <Text style={{ color: c, fontSize: 12 }}>{label}</Text>
          </View>
        );
      }}
    </Pressable>
  );
}

/** Electrum's ButtonContainer: highlight-coloured bar, buttons separated by short darker lines. */
export function ButtonBar({ children, style }: { children: ReactNode; style?: ViewStyle }) {
  const t = useTheme();
  const items = (Array.isArray(children) ? children.flat() : [children]).filter(Boolean);
  return (
    <View style={[{ flexDirection: 'row', alignItems: 'center', backgroundColor: t.card }, style]}>
      {items.map((c, i) => (
        <View key={i} style={{ flex: 1, flexDirection: 'row', alignItems: 'center' }}>
          {i ? <View style={{ width: 2, height: 36, backgroundColor: t.darker }} /> : null}
          {c}
        </View>
      ))}
    </View>
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

/** Coin logo (bundled, see coinLogos.ts), else a pattern from the contract (Identicon.tsx), else a letter badge; optional chain marker in the corner. */
export function CoinIcon({ symbol, size = 32, logo, seed, small, smallLogo }: { symbol: string; size?: number; logo?: ImageSourcePropType; seed?: string; small?: string; smallLogo?: ImageSourcePropType }) {
  const t = useTheme();
  const c = coinColor(symbol);
  const downloaded = useDownloadedLogo(logo ? undefined : seed); // listed tokens, fetched with decoys (logoStore.ts)
  return (
    <View style={{ width: size, height: size }}>
      {logo || downloaded ? (
        <Image source={logo ?? { uri: downloaded }} style={{ width: size, height: size, borderRadius: size / 2 }} accessibilityLabel={symbol} />
      ) : seed ? (
        <View accessibilityLabel={symbol}>
          <Identicon seed={seed} size={size} />
        </View>
      ) : (
        <View style={{ width: size, height: size, borderRadius: size / 2, backgroundColor: c + '33', alignItems: 'center', justifyContent: 'center', overflow: 'hidden', paddingHorizontal: size * 0.12 }}>
          {/* Fixed size inside a fixed circle: iOS "Larger Text" scaled these letters past the circle
              and they were cut off. One line, shrinks to fit. */}
          <Text
            allowFontScaling={false}
            numberOfLines={1}
            adjustsFontSizeToFit
            minimumFontScale={0.6}
            style={{ color: c, fontWeight: '800', fontSize: size * (symbol.length > 3 ? 0.26 : 0.32), lineHeight: size * 0.4, textAlign: 'center' }}>
            {symbol.replace(/[^A-Za-z0-9$₮]/g, '').slice(0, 4).toUpperCase() || '?'}
          </Text>
        </View>
      )}
      {small ? (
        <View style={{ position: 'absolute', right: -3, bottom: -3, backgroundColor: t.bg, borderRadius: 10, padding: 2 }}>
          {smallLogo ? (
            <Image source={smallLogo} style={{ width: 14, height: 14, borderRadius: 7 }} accessibilityLabel={small} />
          ) : (
            <View style={{ width: 14, height: 14, borderRadius: 7, backgroundColor: coinColor(small), alignItems: 'center', justifyContent: 'center' }}>
              <Text allowFontScaling={false} style={{ color: '#fff', fontSize: 8, lineHeight: 10, fontWeight: '800' }}>{small.slice(0, 1)}</Text>
            </View>
          )}
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
    <View style={{ flexDirection: 'row', height: 6, borderRadius: 3, overflow: 'hidden', backgroundColor: t.card, gap: 2 }}>
      {parts.filter((p) => p.value > 0).map((p) => (
        <View key={p.key} style={{ flex: p.value / total, backgroundColor: p.color }} />
      ))}
    </View>
  );
}

/** Bottom sheet (Electrum's menus and dialogs: #424242, radius 8). */
export function Sheet({ visible, onClose, title, children }: { visible: boolean; onClose: () => void; title: string; children: ReactNode }) {
  const t = useTheme();
  return (
    <Modal visible={visible} transparent animationType="slide" onRequestClose={onClose}>
      <Pressable style={{ flex: 1, backgroundColor: '#0009' }} onPress={onClose} />
      <View style={{ backgroundColor: t.card2, borderTopLeftRadius: 8, borderTopRightRadius: 8, padding: 16, paddingBottom: 36, gap: 2 }}>
        <Text style={{ color: t.text, fontSize: 18, fontWeight: '700', marginBottom: 6 }}>{title}</Text>
        {children}
      </View>
    </Modal>
  );
}

export function SheetItem({ icon, title, sub, onPress }: { icon: IconName; title: string; sub: string; onPress: () => void }) {
  return <ListItem icon={icon} title={title} sub={sub} onPress={onPress} />;
}

/** Shown whenever the device is waiting for a physical button press (Electrum ElDialog style). */
export function ConfirmOnDevice({ visible, what, code }: { visible: boolean; what?: string; code?: string | null }) {
  const t = useTheme();
  return (
    <Modal visible={visible} transparent animationType="fade">
      <View style={{ flex: 1, backgroundColor: '#000B', justifyContent: 'center', padding: 24 }}>
        <View style={{ backgroundColor: t.card2, borderRadius: 8, padding: 20, gap: 14 }}>
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
            <Ionicons name={code ? 'link' : 'hardware-chip'} size={22} color={t.accent} />
            <Text style={{ color: t.text, fontSize: 18, fontWeight: '700' }}>{code ? 'Pair with your tredder' : 'Confirm on your device'}</Text>
          </View>
          {code ? (
            <View style={{ backgroundColor: t.darker, borderRadius: 8, paddingVertical: 14, alignItems: 'center' }}>
              <Text style={{ color: t.accent, fontSize: 34, fontFamily: MONO, letterSpacing: 6 }}>{`${code.slice(0, 3)} ${code.slice(3)}`}</Text>
            </View>
          ) : null}
          <T>{what ?? 'Check every detail on the device screen, then press Confirm. Press Back to cancel.'}</T>
          <ActivityIndicator color={t.accent} />
        </View>
      </View>
    </Modal>
  );
}

export const hairline = StyleSheet.hairlineWidth;
