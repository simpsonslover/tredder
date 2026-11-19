/*
 * Gradients and soft edges, drawn with react-native-svg (already a dependency for the QR code).
 *
 * Deliberately NOT expo-linear-gradient: that is a native module, so adding it would mean a fresh
 * `expo prebuild` + `pod install`, which cannot be done on Windows. react-native-svg is already
 * linked into the generated iOS project, so these work in the app as it is built today.
 */
import { useId, useState, type ReactNode } from 'react';
import { StyleSheet, View, type LayoutChangeEvent, type StyleProp, type ViewStyle } from 'react-native';
import Svg, { Defs, LinearGradient, Rect, Stop } from 'react-native-svg';

/**
 * SVG has no 8-digit hex: '#FFFFFF1F' is read as opaque white and the alpha byte is dropped. Split
 * it off and hand it to stopOpacity instead, so '#FFFFFF1F' really is white at 12%.
 */
function splitAlpha(c: string): [string, number] {
  const m = /^#([0-9a-f]{6})([0-9a-f]{2})$/i.exec(c);
  if (m?.[1] && m[2]) return [`#${m[1]}`, parseInt(m[2], 16) / 255];
  const short = /^#([0-9a-f]{3})([0-9a-f])$/i.exec(c);
  if (short?.[1] && short[2]) return [`#${short[1]}`, parseInt(short[2] + short[2], 16) / 255];
  return [c, 1];
}

export type GradientDirection = 'down' | 'up' | 'diagonal' | 'right';

const VECTORS: Record<GradientDirection, { x1: string; y1: string; x2: string; y2: string }> = {
  down: { x1: '0', y1: '0', x2: '0', y2: '1' },
  up: { x1: '0', y1: '1', x2: '0', y2: '0' },
  diagonal: { x1: '0', y1: '0', x2: '1', y2: '1' },
  right: { x1: '0', y1: '0', x2: '1', y2: '0' },
};

/**
 * Absolutely-positioned gradient that fills its parent. The parent needs a position (every View
 * is `relative` by default, so in practice: just put this first among the parent's children).
 *
 * `radius` rounds the gradient itself, so a card does not need `overflow: 'hidden'` (which on iOS
 * would also clip the card's shadow).
 */
export function Gradient({
  colors,
  locations,
  direction = 'down',
  radius = 0,
  opacity,
}: {
  colors: readonly string[];
  /** 0..1 per colour; defaults to an even spread. */
  locations?: readonly number[];
  direction?: GradientDirection;
  radius?: number;
  opacity?: number;
}) {
  const id = `g${useId().replace(/[^a-zA-Z0-9]/g, '')}`;
  const v = VECTORS[direction];
  const [size, setSize] = useState<{ w: number; h: number } | null>(null);
  /*
   * SVG clamps rx to width/2 and ry to height/2 INDEPENDENTLY, so a pill radius (999) on a wide,
   * short box gives rx=width/2, ry=height/2 — an ellipse, not a pill. Clamp both to the same value
   * once the box has been measured. Before that, 24 is close enough to a pill's final radius that
   * the first frame does not visibly jump.
   */
  const r = size ? Math.min(radius, size.w / 2, size.h / 2) : Math.min(radius, 24);
  const onLayout = (e: LayoutChangeEvent) => {
    const { width: w, height: h } = e.nativeEvent.layout;
    if (w !== size?.w || h !== size?.h) setSize({ w, h });
  };
  const stops = colors.map((c, i) => ({
    color: c,
    at: locations?.[i] ?? (colors.length === 1 ? 0 : i / (colors.length - 1)),
  }));
  return (
    <View pointerEvents="none" onLayout={onLayout} style={[StyleSheet.absoluteFill, { opacity }]}>
      <Svg width="100%" height="100%">
        <Defs>
          <LinearGradient id={id} x1={v.x1} y1={v.y1} x2={v.x2} y2={v.y2}>
            {stops.map((s, i) => {
              const [color, alpha] = splitAlpha(s.color);
              return <Stop key={i} offset={s.at} stopColor={color} stopOpacity={alpha} />;
            })}
          </LinearGradient>
        </Defs>
        <Rect x="0" y="0" width="100%" height="100%" rx={r} ry={r} fill={`url(#${id})`} />
      </Svg>
    </View>
  );
}

/**
 * A surface with a gradient behind its children: the building block for cards, bars and sheets.
 * Children keep their own layout; the gradient and the hairline sit underneath.
 */
export function GradientView({
  colors,
  locations,
  direction = 'down',
  radius = 0,
  border,
  style,
  children,
}: {
  colors: readonly string[];
  locations?: readonly number[];
  direction?: GradientDirection;
  radius?: number;
  /** Hairline drawn on top of the gradient, at the same radius. */
  border?: string;
  style?: StyleProp<ViewStyle>;
  children?: ReactNode;
}) {
  return (
    <View style={[{ borderRadius: radius }, style]}>
      <Gradient colors={colors} locations={locations} direction={direction} radius={radius} />
      {border ? (
        <View
          pointerEvents="none"
          style={[StyleSheet.absoluteFill, { borderRadius: radius, borderWidth: StyleSheet.hairlineWidth, borderColor: border }]}
        />
      ) : null}
      {children}
    </View>
  );
}
