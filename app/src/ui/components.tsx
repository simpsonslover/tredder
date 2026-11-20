/*
 * UI kit in the style of Electrum's mobile app (electrum/gui/qml, MIT licence): Qt Material Dark
 * (#303030 background, #424242 toolbars and dialogs, light-blue accent), flat toolbars, rounded
 * "highlight panes", monospace amounts in green / red, accent-coloured section labels, and flat
 * icon-over-text button bars with thin separators. Values below are taken from Electrum's
 * Constants.qml / main.qml; the icons are Ionicons, not Electrum's PNGs.
 */
import Ionicons from '@expo/vector-icons/Ionicons';
import { useState, type ComponentProps, type ReactNode } from 'react';
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

import { Gradient, GradientView } from './gradient';
import { Identicon } from './Identicon';
import { useDownloadedLogo } from './logoStore';

/**
 * Monochrome dark skin: black-to-graphite gradients, generous corner radii, hairline edges in
 * white alpha, and a platinum accent instead of a hue. Only the semantic colours (credit, debit,
 * warning) keep a hue — in a wallet those carry meaning, so they must not wash out to grey.
 */
const THEME = {
  dark: true,
  bg: '#0B0B0D', //          page base, under the gradient
  card: '#1A1B20', //        flat fallback for a raised surface
  card2: '#141519', //       bars, sheets, dialogs
  darker: '#070709', //      wells (inset fields, code blocks)
  text: '#F4F5F7',
  muted: '#8A8F98',
  border: '#FFFFFF14', //    hairline, white at ~8%
  accent: '#D8DDE6', //      platinum: section labels, affordances
  accent2: '#9AA2AE',
  onAccent: '#0B0B0D', //    text on a platinum fill
  danger: '#FF7B72',
  warn: '#E3B341',
  ok: '#56D364',

  /* Gradients: [from, …, to] along the stated direction. */
  bgGrad: ['#1C1D22', '#101116', '#08080A'] as const, //   page: graphite at the top → black
  cardGrad: ['#24262D', '#171821'] as const, //            raised surface
  listGrad: ['#1C1E25', '#191A21'] as const, //            list card: nearly flat, so the top row
  //                                                       does not read as "selected"
  card2Grad: ['#1A1B21', '#101116'] as const, //           bars and sheets
  heroGrad: ['#2B2E37', '#15161C', '#0E0F13'] as const, // the balance card
  primaryGrad: ['#34373F', '#1B1D24'] as const, //         primary button: graphite, lit at the top
  accentGrad: ['#FFFFFF', '#CFD5DF'] as const, //          platinum fill, for the rare light control
  wellGrad: ['#0A0A0D', '#101116'] as const, //            inset

  /* Edges. Two weights: a plain hairline, and a brighter one for the lit top edge of a card. */
  line: '#FFFFFF14',
  lineStrong: '#FFFFFF24',

  /* Corner radii. Everything rounds; nothing is square. */
  r: { sm: 10, md: 14, lg: 20, xl: 26, pill: 999 },
};
export type Theme = typeof THEME;
export function useTheme(): Theme {
  return THEME;
}

/** Soft drop shadow. iOS renders shadow*, Android elevation; both are set. */
export function shadow(level: 1 | 2 | 3 = 1): ViewStyle {
  const [opacity, radius, dy, elevation] = level === 1 ? [0.2, 6, 2, 2] : level === 2 ? [0.3, 14, 6, 6] : [0.45, 26, 12, 14];
  return { shadowColor: '#000', shadowOpacity: opacity, shadowRadius: radius, shadowOffset: { width: 0, height: dy }, elevation };
}

export const MONO = Platform.select({ ios: 'Menlo', default: 'monospace' });

export type IconName = ComponentProps<typeof Ionicons>['name'];

/** Electrum's ToolBar: #424242, 48 high, small icon + bold title on the left, status on the right. */
export function Toolbar({ title, icon, right, onBack }: { title: string; icon?: IconName; right?: ReactNode; onBack?: () => void }) {
  const t = useTheme();
  return (
    <View
      style={{
        height: 56,
        flexDirection: 'row',
        alignItems: 'center',
        paddingLeft: onBack ? 8 : 20,
        paddingRight: 10,
        gap: 10,
        borderBottomWidth: StyleSheet.hairlineWidth,
        borderBottomColor: t.line,
        zIndex: 2,
      }}
    >
      {onBack ? (
        <Pressable accessibilityRole="button" accessibilityLabel="Back" onPress={onBack} hitSlop={12} style={({ pressed }) => [{ padding: 8, borderRadius: t.r.pill }, pressed && { backgroundColor: '#FFFFFF14' }]}>
          <Ionicons name="arrow-back" size={22} color={t.text} />
        </Pressable>
      ) : icon ? (
        <View style={{ width: 30, height: 30, borderRadius: t.r.pill, alignItems: 'center', justifyContent: 'center', borderWidth: StyleSheet.hairlineWidth, borderColor: t.lineStrong }}>
          <Gradient colors={t.cardGrad} radius={15} />
          <Ionicons name={icon} size={15} color={t.accent} />
        </View>
      ) : null}
      <Text numberOfLines={1} style={{ flex: 1, color: t.text, fontSize: 18, fontWeight: '700', letterSpacing: 0.2 }}>
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
    <View style={{ flex: 1, backgroundColor: t.bg }}>
      {/* One gradient for the whole page: graphite at the top, black at the bottom. */}
      <Gradient colors={t.bgGrad} locations={[0, 0.45, 1]} />
      <SafeAreaView style={{ flex: 1 }} edges={title ? ['top'] : []}>
        {title ? <Toolbar title={title} icon={icon} right={right} /> : null}
        <ScrollView
          style={{ backgroundColor: 'transparent' }}
          contentContainerStyle={{ paddingHorizontal: 14, paddingTop: 14, gap: 14, paddingBottom: 40 }}
          refreshControl={onRefresh ? <RefreshControl tintColor={t.muted} colors={[t.accent]} progressBackgroundColor={t.card2} refreshing={!!refreshing} onRefresh={onRefresh} /> : undefined}
        >
          {children}
        </ScrollView>
        {footer}
      </SafeAreaView>
    </View>
  );
}

/** Raised surface: graphite gradient, large radius, lit hairline edge, soft shadow. */
export function Card({ children, style, hero }: { children: ReactNode; style?: ViewStyle; hero?: boolean }) {
  const t = useTheme();
  return (
    <GradientView
      colors={hero ? t.heroGrad : t.cardGrad}
      direction={hero ? 'diagonal' : 'down'}
      radius={hero ? t.r.xl : t.r.lg}
      border={hero ? t.lineStrong : t.line}
      style={[{ padding: 16, gap: 10 }, shadow(hero ? 2 : 1), style]}
    >
      {children}
    </GradientView>
  );
}
export const Pane = Card;

type TextKind = 'hero' | 'title' | 'h2' | 'body' | 'muted' | 'mono' | 'error' | 'warn' | 'ok' | 'section';
export function T({ children, kind = 'body', center, selectable, color }: { children: ReactNode; kind?: TextKind; center?: boolean; selectable?: boolean; color?: string }) {
  const t = useTheme();
  const s: Record<TextKind, object> = {
    hero: { fontSize: 30, color: t.text, fontFamily: MONO, letterSpacing: -0.5 },
    title: { fontSize: 22, color: t.text, fontWeight: '700' },
    h2: { fontSize: 16, color: t.text },
    /* small, tracked, uppercase: a quiet label above a card rather than a coloured heading */
    section: { fontSize: 11.5, color: t.accent2, marginTop: 4, marginLeft: 6, textTransform: 'uppercase', letterSpacing: 1.1, fontWeight: '700' },
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
  /* every kind is a dark pill; primary is lighter graphite with a lit edge and a deeper shadow. */
  const fg = kind === 'danger' ? t.danger : t.text;
  return (
    <Pressable
      accessibilityRole="button"
      onPress={onPress}
      disabled={disabled || loading}
      style={({ pressed }) => [
        {
          minHeight: 52,
          borderRadius: t.r.pill,
          paddingHorizontal: 20,
          alignItems: 'center',
          justifyContent: 'center',
          flexDirection: 'row',
          gap: 8,
        },
        kind === 'primary' ? shadow(2) : null,
        pressed && { opacity: 0.75, transform: [{ scale: 0.985 }] },
        disabled && { opacity: 0.4 },
      ]}
    >
      <Gradient colors={kind === 'primary' ? t.primaryGrad : t.cardGrad} radius={t.r.pill} />
      <View
        pointerEvents="none"
        style={[
          StyleSheet.absoluteFill,
          {
            borderRadius: t.r.pill,
            borderWidth: StyleSheet.hairlineWidth,
            borderColor: kind === 'danger' ? t.danger + '55' : kind === 'primary' ? '#FFFFFF33' : t.line,
          },
        ]}
      />
      {loading ? (
        <ActivityIndicator color={fg} />
      ) : (
        <>
          {icon ? <Ionicons name={icon} size={19} color={fg} /> : null}
          <Text style={{ color: fg, fontWeight: '700', fontSize: 15.5, letterSpacing: 0.2 }}>{title}</Text>
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

/** Section label above a rounded card whose rows are split by hairlines. */
export function ListGroup({ title, children, footer }: { title?: string; children: ReactNode; footer?: string }) {
  const t = useTheme();
  const items = (Array.isArray(children) ? children.flat() : [children]).filter(Boolean);
  return (
    <View style={{ gap: 8 }}>
      {title ? <T kind="section">{title}</T> : null}
      <View style={[{ borderRadius: t.r.lg }, shadow(1)]}>
        <GradientView colors={t.listGrad} radius={t.r.lg} border={t.line} style={{ paddingHorizontal: 10, overflow: 'hidden' }}>
          {items.map((c, i) => (
            <View key={i} style={i ? { borderTopWidth: StyleSheet.hairlineWidth, borderTopColor: t.line } : undefined}>
              {c}
            </View>
          ))}
        </GradientView>
      </View>
      {footer ? <View style={{ paddingHorizontal: 6 }}><T kind="muted">{footer}</T></View> : null}
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
    <GradientView colors={t.cardGrad} radius={t.r.lg} style={[{ flexDirection: 'row', gap: 12, padding: 14 }, shadow(1)]}>
      {/* the status colour reads as a soft wash from the left edge, not a hard 3 px rule */}
      <Gradient colors={[c + '2E', c + '00']} direction="right" radius={t.r.lg} />
      <View pointerEvents="none" style={[StyleSheet.absoluteFill, { borderRadius: t.r.lg, borderWidth: StyleSheet.hairlineWidth, borderColor: c + '40' }]} />
      <Ionicons name={icon ?? (kind === 'error' ? 'alert-circle' : kind === 'info' ? 'information-circle' : 'warning')} size={20} color={c} />
      <View style={{ flex: 1, gap: 4 }}>{typeof children === 'string' ? <T>{children}</T> : children}</View>
    </GradientView>
  );
}

export function Badge({ label, color }: { label: string; color?: string }) {
  const t = useTheme();
  const c = color ?? t.accent;
  return (
    <View style={{ alignSelf: 'flex-start', borderWidth: StyleSheet.hairlineWidth, borderColor: c + '66', borderRadius: t.r.pill, paddingHorizontal: 9, paddingVertical: 3, backgroundColor: c + '14' }}>
      <Text style={{ color: c, fontSize: 10.5, fontWeight: '700', letterSpacing: 0.6, textTransform: 'uppercase' }}>{label}</Text>
    </View>
  );
}

/** Label above an inset, rounded well (replaces the single underline). */
export function Field(props: TextInputProps & { label: string }) {
  const t = useTheme();
  const [focus, setFocus] = useState(false);
  const { label, onFocus, onBlur, style, ...rest } = props;
  return (
    <View style={{ gap: 7 }}>
      <Text style={{ color: t.accent2, fontSize: 11.5, fontWeight: '700', letterSpacing: 1, textTransform: 'uppercase', marginLeft: 4 }}>{label}</Text>
      <View style={{ borderRadius: t.r.md, overflow: 'hidden' }}>
        <Gradient colors={t.wellGrad} radius={t.r.md} />
        <View pointerEvents="none" style={[StyleSheet.absoluteFill, { borderRadius: t.r.md, borderWidth: focus ? 1 : StyleSheet.hairlineWidth, borderColor: focus ? t.accent + '80' : t.line }]} />
        <TextInput
          autoCapitalize="none"
          autoCorrect={false}
          placeholderTextColor={t.muted}
          selectionColor={t.accent}
          onFocus={(e) => (setFocus(true), onFocus?.(e))}
          onBlur={(e) => (setFocus(false), onBlur?.(e))}
          {...rest}
          style={[{ paddingHorizontal: 14, paddingVertical: 14, color: t.text, fontSize: 15 }, style]}
        />
      </View>
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

/** Icon over a small label; the selected one sits on a rounded pill. */
export function ActionButton({ icon, label, onPress, disabled, selected }: { icon: IconName; label: string; onPress: () => void; disabled?: boolean; selected?: boolean; onGradient?: boolean }) {
  const t = useTheme();
  return (
    <Pressable onPress={onPress} disabled={disabled} accessibilityRole="button" accessibilityState={{ selected }} style={{ flex: 1 }}>
      {({ pressed }) => {
        const c = disabled ? t.muted + '80' : selected ? t.text : pressed ? t.accent : t.muted;
        return (
          <View style={{ alignItems: 'center', gap: 4, paddingVertical: 9, paddingHorizontal: 2, borderRadius: t.r.md, overflow: 'hidden' }}>
            {selected ? (
              <>
                <Gradient colors={['#FFFFFF1F', '#FFFFFF08']} radius={t.r.md} />
                <View pointerEvents="none" style={[StyleSheet.absoluteFill, { borderRadius: t.r.md, borderWidth: StyleSheet.hairlineWidth, borderColor: t.lineStrong }]} />
              </>
            ) : null}
            <Ionicons name={icon} size={22} color={c} />
            <Text numberOfLines={1} style={{ color: c, fontSize: 11, fontWeight: selected ? '700' : '500' }}>{label}</Text>
          </View>
        );
      }}
    </Pressable>
  );
}

/** Row of ActionButtons, evenly split, no separators (the selected pill does the work). */
export function ButtonBar({ children, style }: { children: ReactNode; style?: ViewStyle }) {
  const items = (Array.isArray(children) ? children.flat() : [children]).filter(Boolean);
  return (
    <View style={[{ flexDirection: 'row', alignItems: 'center', gap: 4 }, style]}>
      {items.map((c, i) => (
        <View key={i} style={{ flex: 1, flexDirection: 'row', alignItems: 'center' }}>
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
    <View style={{ flexDirection: 'row', height: 7, borderRadius: 4, overflow: 'hidden', backgroundColor: t.darker, gap: 2.5 }}>
      {parts.filter((p) => p.value > 0).map((p) => (
        <View key={p.key} style={{ flex: p.value / total, backgroundColor: p.color, borderRadius: 4 }} />
      ))}
    </View>
  );
}

/** Bottom sheet (Electrum's menus and dialogs: #424242, radius 8). */
export function Sheet({ visible, onClose, title, children }: { visible: boolean; onClose: () => void; title: string; children: ReactNode }) {
  const t = useTheme();
  return (
    <Modal visible={visible} transparent animationType="slide" onRequestClose={onClose}>
      <Pressable style={{ flex: 1, backgroundColor: '#000000B8' }} onPress={onClose} />
      <View style={{ borderTopLeftRadius: t.r.xl, borderTopRightRadius: t.r.xl, overflow: 'hidden' }}>
        <Gradient colors={t.card2Grad} direction="down" />
        <View style={[StyleSheet.absoluteFill, { borderTopLeftRadius: t.r.xl, borderTopRightRadius: t.r.xl, borderWidth: StyleSheet.hairlineWidth, borderColor: t.lineStrong }]} pointerEvents="none" />
        <View style={{ paddingHorizontal: 16, paddingTop: 10, paddingBottom: 38, gap: 2 }}>
          {/* grab handle */}
          <View style={{ alignSelf: 'center', width: 38, height: 4, borderRadius: 2, backgroundColor: t.lineStrong, marginBottom: 12 }} />
          <Text style={{ color: t.text, fontSize: 19, fontWeight: '700', marginBottom: 6, marginLeft: 4 }}>{title}</Text>
          {children}
        </View>
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
      <View style={{ flex: 1, backgroundColor: '#000000CC', justifyContent: 'center', padding: 24 }}>
        <GradientView colors={t.heroGrad} direction="diagonal" radius={t.r.xl} border={t.lineStrong} style={[{ padding: 22, gap: 14 }, shadow(3)]}>
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
            <Ionicons name={code ? 'link' : 'shield-checkmark'} size={22} color={t.accent} />
            <Text style={{ color: t.text, fontSize: 18, fontWeight: '700' }}>{code ? 'Pair with your tredder' : 'Confirm on your device'}</Text>
          </View>
          {code ? (
            <View style={{ borderRadius: t.r.md, paddingVertical: 16, alignItems: 'center', overflow: 'hidden' }}>
              <Gradient colors={t.wellGrad} radius={t.r.md} />
              <Text style={{ color: t.accent, fontSize: 34, fontFamily: MONO, letterSpacing: 6 }}>{`${code.slice(0, 3)} ${code.slice(3)}`}</Text>
            </View>
          ) : null}
          <T>{what ?? 'Check every detail on the device screen, then press Confirm. Press Back to cancel.'}</T>
          <ActivityIndicator color={t.accent} />
        </GradientView>
      </View>
    </Modal>
  );
}

/**
 * Pairing: the user types the code the tredder shows; the app checks it against the code of its
 * own secure session (both come from the Noise handshake, so a device-in-the-middle shows a
 * different one). A match only tells the user it is safe to accept ON THE DEVICE (✓ + PIN): the
 * device itself decides, because any phone knows its own code.
 */
export function PairWithCode({ visible, code, onCancel }: { visible: boolean; code: string | null; onCancel: () => void }) {
  const t = useTheme();
  const [typed, setTyped] = useState('');
  const done = typed.length === 6;
  const ok = done && !!code && typed === code;
  return (
    <Modal visible={visible} transparent animationType="fade" onRequestClose={onCancel}>
      <View style={{ flex: 1, backgroundColor: '#000000CC', justifyContent: 'center', padding: 24 }}>
        <GradientView colors={t.heroGrad} direction="diagonal" radius={t.r.xl} border={t.lineStrong} style={[{ padding: 22, gap: 14 }, shadow(3)]}>
          <View style={{ flexDirection: 'row', alignItems: 'center', gap: 10 }}>
            <Ionicons name="link" size={22} color={t.accent} />
            <Text style={{ color: t.text, fontSize: 18, fontWeight: '700' }}>Pair with your tredder</Text>
          </View>
          <T>Type the 6-digit code shown on your tredder.</T>
          <TextInput
            value={typed}
            onChangeText={(v) => setTyped(v.replace(/\D/g, '').slice(0, 6))}
            keyboardType="number-pad"
            maxLength={6}
            autoFocus
            textContentType="oneTimeCode"
            placeholder="000000"
            placeholderTextColor={t.muted}
            style={{ backgroundColor: t.darker, borderRadius: t.r.md, borderWidth: StyleSheet.hairlineWidth, borderColor: ok ? t.ok + '80' : done ? t.danger + '80' : t.lineStrong, paddingVertical: 14, color: ok ? t.ok : done ? t.danger : t.accent, fontSize: 32, fontFamily: MONO, letterSpacing: 8, textAlign: 'center' }}
          />
          {!done ? null : ok ? (
            <View style={{ flexDirection: 'row', gap: 10, alignItems: 'center' }}>
              <Ionicons name="checkmark-circle" size={22} color={t.ok} />
              <View style={{ flex: 1 }}>
                <T>Code is correct. Now press ✓ accept on your tredder and enter your PIN.</T>
              </View>
            </View>
          ) : (
            <View style={{ flexDirection: 'row', gap: 10, alignItems: 'center' }}>
              <Ionicons name="close-circle" size={22} color={t.danger} />
              <View style={{ flex: 1 }}>
                <T>That is not this connection's code. Check it, or press ✗ reject on the tredder: something may be between your phone and the device.</T>
              </View>
            </View>
          )}
          <View style={{ flexDirection: 'row', gap: 10 }}>
            {done && !ok ? (
              <View style={{ flex: 1 }}>
                <Button kind="secondary" title="Type again" onPress={() => setTyped('')} />
              </View>
            ) : null}
            <View style={{ flex: 1 }}>
              <Button kind="secondary" title="Cancel" onPress={onCancel} />
            </View>
          </View>
          {ok ? <ActivityIndicator color={t.accent} /> : null}
        </GradientView>
      </View>
    </Modal>
  );
}

export const hairline = StyleSheet.hairlineWidth;
