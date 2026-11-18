// Shows a caught JavaScript error (crashCatcher.ts) instead of the app closing. Plain components and
// fixed colours on purpose: it must still draw when the theme or wallet code is what failed.
import { Component, useEffect, useState, type ReactNode } from 'react';
import { ScrollView, Text, View } from 'react-native';

import { lastCaught, onCaught, report, type Caught } from '../crashCatcher';

function ErrorText({ c }: { c: Caught }) {
  return (
    <View style={{ flex: 1, backgroundColor: '#1e1e1e', paddingTop: 70, paddingHorizontal: 16 }}>
      <Text style={{ color: '#ff6b6b', fontSize: 20, fontWeight: '700', marginBottom: 8 }}>Tredder hit an error</Text>
      <Text style={{ color: '#cccccc', fontSize: 14, marginBottom: 12 }}>Take a screenshot of this screen. Your wallet is safe: keys are only on the device.</Text>
      <ScrollView style={{ flex: 1 }}>
        <Text selectable style={{ color: '#ffffff', fontSize: 15, fontWeight: '700', marginBottom: 10 }}>{c.message}</Text>
        <Text selectable style={{ color: '#aaaaaa', fontSize: 11, fontFamily: 'Menlo' }}>{c.stack ?? ''}</Text>
      </ScrollView>
    </View>
  );
}

class Boundary extends Component<{ children: ReactNode }, { c: Caught | null }> {
  state = { c: null as Caught | null };
  static getDerivedStateFromError(e: unknown) {
    const err = e instanceof Error ? e : new Error(String(e));
    return { c: { message: `${err.name}: ${err.message}`, stack: err.stack, fatal: true } };
  }
  componentDidCatch(e: unknown, info: { componentStack?: string | null }) {
    report(Object.assign(e instanceof Error ? e : new Error(String(e)), { stack: `${(e as Error)?.stack ?? ''}\n${info.componentStack ?? ''}` }), true);
  }
  render() {
    return this.state.c ? <ErrorText c={this.state.c} /> : this.props.children;
  }
}

/** Wraps the whole app: render errors and uncaught errors elsewhere both end up on screen. */
export function CrashGuard({ children }: { children: ReactNode }) {
  const [c, setC] = useState<Caught | null>(lastCaught());
  useEffect(() => onCaught((x) => x.fatal && setC({ ...x })), []);
  return c ? <ErrorText c={c} /> : <Boundary>{children}</Boundary>;
}
