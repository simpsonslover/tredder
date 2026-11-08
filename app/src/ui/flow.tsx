/* Building blocks for the step-by-step send / receive screens. */
import Ionicons from '@expo/vector-icons/Ionicons';
import { CameraView, useCameraPermissions } from 'expo-camera';
import { useRef, type ReactNode } from 'react';
import { Linking, Modal, Pressable, ScrollView, Text, View } from 'react-native';
import { SafeAreaView } from 'react-native-safe-area-context';

import type { Key } from '../util/amountInput';
import { addressParts } from '../util/recipient';
import { Button, T, Toolbar, useTheme } from './components';

/** Full-height step: header, scrolling body, buttons pinned at the bottom. */
export function StepScreen({ title, onBack, right, children, footer }: { title: string; onBack?: () => void; right?: ReactNode; children: ReactNode; footer?: ReactNode }) {
  const t = useTheme();
  return (
    <SafeAreaView style={{ flex: 1, backgroundColor: t.card2 }} edges={['top']}>
      <Toolbar title={title} onBack={onBack} right={right} />
      <ScrollView style={{ backgroundColor: t.bg }} keyboardShouldPersistTaps="handled" contentContainerStyle={{ paddingHorizontal: 14, paddingTop: 12, gap: 12, paddingBottom: 24, flexGrow: 1 }}>
        {children}
      </ScrollView>
      {footer ? <View style={{ paddingHorizontal: 14, paddingTop: 8, paddingBottom: 12, gap: 8, backgroundColor: t.bg }}>{footer}</View> : null}
    </SafeAreaView>
  );
}

const KEYS: Key[][] = [
  ['1', '2', '3'],
  ['4', '5', '6'],
  ['7', '8', '9'],
  ['.', '0', 'del'],
];

/** Phone-style number pad (no system keyboard, so the amount stays big and readable). */
export function Keypad({ onKey, decimals }: { onKey: (k: Key) => void; decimals: number }) {
  const t = useTheme();
  return (
    <View style={{ gap: 4 }}>
      {KEYS.map((row, i) => (
        <View key={i} style={{ flexDirection: 'row' }}>
          {row.map((k) => (
            <Pressable
              key={k}
              accessibilityRole="button"
              accessibilityLabel={k === 'del' ? 'Delete' : k === '.' ? 'Decimal point' : k}
              disabled={k === '.' && decimals === 0}
              onPress={() => onKey(k)}
              style={({ pressed }) => [{ flex: 1, height: 56, alignItems: 'center', justifyContent: 'center', borderRadius: 14 }, pressed && { backgroundColor: t.card2 }]}
            >
              {k === 'del' ? (
                <Ionicons name="backspace-outline" size={24} color={t.text} />
              ) : (
                <Text style={{ color: k === '.' && decimals === 0 ? t.border : t.text, fontSize: 26, fontWeight: '500' }}>{k}</Text>
              )}
            </Pressable>
          ))}
        </View>
      ))}
    </View>
  );
}

/** Full address with the first and last 6 characters highlighted: what you compare on the device. */
export function AddressText({ address, center, size = 15, selectable = true }: { address: string; center?: boolean; size?: number; selectable?: boolean }) {
  const t = useTheme();
  const [a, mid, b] = addressParts(address);
  return (
    <Text selectable={selectable} style={{ fontFamily: 'monospace', fontSize: size, lineHeight: size * 1.45, color: t.muted, textAlign: center ? 'center' : 'left' }}>
      <Text style={{ color: t.text, fontWeight: '700' }}>{a}</Text>
      {mid}
      <Text style={{ color: t.text, fontWeight: '700' }}>{b}</Text>
    </Text>
  );
}

/** Round icon button, small (paste / scan next to the address field). */
export function IconPill({ icon, label, onPress }: { icon: React.ComponentProps<typeof Ionicons>['name']; label: string; onPress: () => void }) {
  const t = useTheme();
  return (
    <Pressable accessibilityRole="button" accessibilityLabel={label} onPress={onPress} style={({ pressed }) => [{ flexDirection: 'row', alignItems: 'center', gap: 6, backgroundColor: t.card2, borderRadius: 999, paddingHorizontal: 12, paddingVertical: 8 }, pressed && { opacity: 0.6 }]}>
      <Ionicons name={icon} size={16} color={t.text} />
      <Text style={{ color: t.text, fontSize: 13, fontWeight: '600' }}>{label}</Text>
    </Pressable>
  );
}

/** Full-screen QR scanner. Calls onScan once with the raw text. */
export function Scanner({ visible, onClose, onScan }: { visible: boolean; onClose: () => void; onScan: (text: string) => void }) {
  const t = useTheme();
  const [perm, request] = useCameraPermissions();
  const done = useRef(false);
  return (
    <Modal visible={visible} animationType="slide" onRequestClose={onClose} onShow={() => {
      done.current = false;
    }}>
      <View style={{ flex: 1, backgroundColor: '#000' }}>
        {perm?.granted ? (
          <CameraView
            style={{ flex: 1 }}
            facing="back"
            barcodeScannerSettings={{ barcodeTypes: ['qr'] }}
            onBarcodeScanned={({ data }) => {
              if (done.current) return;
              done.current = true;
              onScan(data);
            }}
          />
        ) : (
          <View style={{ flex: 1, justifyContent: 'center', padding: 32, gap: 16 }}>
            <T kind="h2" center>Camera access</T>
            <T kind="muted" center>The camera is used only to read the address QR code. Nothing is recorded or sent anywhere.</T>
            <Button title={perm?.canAskAgain === false ? 'Open Settings' : 'Allow camera'} onPress={() => void (perm?.canAskAgain === false ? Linking.openSettings() : request())} />
          </View>
        )}
        {perm?.granted ? (
          <View pointerEvents="none" style={{ position: 'absolute', top: 0, bottom: 0, left: 0, right: 0, alignItems: 'center', justifyContent: 'center' }}>
            <View style={{ width: 240, height: 240, borderRadius: 24, borderWidth: 3, borderColor: t.accent }} />
            <Text style={{ color: '#fff', marginTop: 20, fontSize: 15 }}>Point at an address QR code</Text>
          </View>
        ) : null}
        <SafeAreaView style={{ position: 'absolute', top: 0, left: 0 }}>
          <Pressable accessibilityRole="button" accessibilityLabel="Close scanner" onPress={onClose} style={{ margin: 16, width: 44, height: 44, borderRadius: 22, backgroundColor: '#0008', alignItems: 'center', justifyContent: 'center' }}>
            <Ionicons name="close" size={26} color="#fff" />
          </Pressable>
        </SafeAreaView>
      </View>
    </Modal>
  );
}
