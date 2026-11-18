// Loaded first (index.ts). In a release build an uncaught JavaScript error closes the app with no
// message. This catches it instead and keeps it for CrashScreen (ui/CrashScreen.tsx), which shows
// the text so it can be read off the phone. Shown only on this phone; nothing is sent anywhere.

export type Caught = { message: string; stack?: string; fatal: boolean };

let caught: Caught | null = null;
const subs = new Set<(c: Caught) => void>();

export function report(e: unknown, fatal: boolean): void {
  const err = e instanceof Error ? e : new Error(String(e));
  if (caught?.fatal && !fatal) return; // keep the first fatal one
  caught = { message: `${err.name}: ${err.message}`, stack: err.stack, fatal };
  subs.forEach((f) => f(caught!));
}

export function lastCaught(): Caught | null {
  return caught;
}

export function onCaught(f: (c: Caught) => void): () => void {
  subs.add(f);
  return () => void subs.delete(f);
}

type Handler = (e: unknown, fatal?: boolean) => void;
const EU = (globalThis as unknown as { ErrorUtils?: { getGlobalHandler(): Handler; setGlobalHandler(h: Handler): void } }).ErrorUtils;
if (EU && !__DEV__) {
  // dev builds keep React Native's own red screen
  EU.setGlobalHandler((e, fatal) => report(e, !!fatal));
}
