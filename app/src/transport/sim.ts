// Development-only transport to the desktop tredder simulator (sim/).
// Same frames as BLE, carried over WebSocket. On the Android emulator the
// devstack script runs `adb reverse tcp:8765 tcp:8765`, so localhost reaches the PC.
import type { Transport } from './types';

export const DEFAULT_SIM_URL = 'ws://localhost:8765';

export class SimTransport implements Transport {
  readonly kind = 'sim' as const;
  readonly frameSize = 244;
  readonly name = 'tredder simulator';
  private frameCbs = new Set<(f: Uint8Array) => void>();
  private discCbs = new Set<() => void>();

  private constructor(private readonly ws: WebSocket) {
    ws.binaryType = 'arraybuffer';
    ws.onmessage = (e) => {
      if (e.data instanceof ArrayBuffer) {
        const f = new Uint8Array(e.data);
        this.frameCbs.forEach((cb) => cb(f));
      }
    };
    ws.onclose = () => this.discCbs.forEach((cb) => cb());
  }

  static connect(url = DEFAULT_SIM_URL, timeoutMs = 5000): Promise<SimTransport> {
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(url);
      const t = setTimeout(() => {
        ws.close();
        reject(new Error(`Simulator not reachable at ${url}. Is it running (tools/devstack)?`));
      }, timeoutMs);
      ws.onopen = () => {
        clearTimeout(t);
        resolve(new SimTransport(ws));
      };
      ws.onerror = () => {
        clearTimeout(t);
        reject(new Error(`Simulator not reachable at ${url}. Is it running (tools/devstack)?`));
      };
    });
  }

  async send(frame: Uint8Array): Promise<void> {
    if (this.ws.readyState !== WebSocket.OPEN) throw new Error('simulator connection closed');
    this.ws.send(new Uint8Array(frame)); // copy: guarantees an ArrayBuffer-backed view
  }

  onFrame(cb: (f: Uint8Array) => void) {
    this.frameCbs.add(cb);
    return () => this.frameCbs.delete(cb);
  }

  onDisconnect(cb: () => void) {
    this.discCbs.add(cb);
    return () => this.discCbs.delete(cb);
  }

  async close() {
    this.ws.close();
  }
}
