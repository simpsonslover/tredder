// A frame-level link to a wallet device (BLE, or the in-app mock).
export interface Transport {
  readonly kind: 'ble' | 'mock' | 'sim';
  /** True once the Noise secure channel (protocol §3) is established. */
  readonly secure: boolean;
  /** Max bytes per frame (BLE: ATT MTU - 3). */
  readonly frameSize: number;
  readonly name: string;
  send(frame: Uint8Array): Promise<void>;
  onFrame(cb: (frame: Uint8Array) => void): () => void;
  onDisconnect(cb: () => void): () => void;
  close(): Promise<void>;
}
