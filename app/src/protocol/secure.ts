// Noise XX handshake over a frame transport (protocol/PROTOCOL.md §3). The handshake
// messages are framed like any other message (§2); everything after it is sealed with
// the session keys by DeviceClient.
import type { Transport } from '../transport/types';
import { Reassembler, toFrames } from './framing';
import { XXInitiator, type Session } from './noise';

export const HANDSHAKE_TIMEOUT_MS = 10_000;

export async function handshake(t: Transport, staticKey: Uint8Array, timeoutMs = HANDSHAKE_TIMEOUT_MS): Promise<Session> {
  const hs = new XXInitiator(staticKey);
  const rx = new Reassembler();
  let unsub: (() => void) | undefined;
  let unsubDisc: (() => void) | undefined;
  let timer: ReturnType<typeof setTimeout> | undefined;
  const msg2 = new Promise<Uint8Array>((resolve, reject) => {
    timer = setTimeout(() => reject(new Error('The device did not answer the secure handshake.')), timeoutMs);
    unsubDisc = t.onDisconnect(() => reject(new Error('Device disconnected during the secure handshake.')));
    unsub = t.onFrame((f) => {
      try {
        const m = rx.push(f);
        if (m) resolve(m);
      } catch {
        reject(new Error('Malformed handshake reply.'));
      }
    });
  });
  try {
    for (const f of toFrames(hs.msg1(), t.frameSize)) await t.send(f);
    hs.readMsg2(await msg2); // throws if the device's reply fails authentication
    for (const f of toFrames(hs.msg3(), t.frameSize)) await t.send(f);
    return hs.split();
  } finally {
    clearTimeout(timer);
    unsub?.();
    unsubDisc?.();
  }
}
