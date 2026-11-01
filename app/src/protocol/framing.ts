// BLE framing (protocol/PROTOCOL.md §2): split messages into MTU-sized frames
// and reassemble them strictly. Any violation drops the partial message.

export const MAX_MESSAGE = 262_144;
const FIRST = 0x80;
const LAST = 0x40;

export function toFrames(message: Uint8Array, frameSize: number): Uint8Array[] {
  if (frameSize < 20) throw new Error('frame size must be >= 20');
  if (message.length > MAX_MESSAGE) throw new Error('message too large');
  const frames: Uint8Array[] = [];
  const firstCap = frameSize - 5;
  const contCap = frameSize - 2;
  let off = Math.min(firstCap, message.length);
  const first = new Uint8Array(5 + off);
  first[0] = FIRST | (off === message.length ? LAST : 0);
  new DataView(first.buffer).setUint32(1, message.length);
  first.set(message.subarray(0, off), 5);
  frames.push(first);
  let seq = 1;
  while (off < message.length) {
    const n = Math.min(contCap, message.length - off);
    const f = new Uint8Array(2 + n);
    f[0] = off + n === message.length ? LAST : 0;
    f[1] = seq;
    f.set(message.subarray(off, off + n), 2);
    frames.push(f);
    off += n;
    seq = seq === 255 ? 1 : seq + 1;
  }
  return frames;
}

export class Reassembler {
  private buf: Uint8Array | null = null;
  private filled = 0;
  private seq = 0;

  /** Feed one frame. Returns a complete message, null if more frames are needed.
   *  Throws on protocol violations (state is reset first). */
  push(frame: Uint8Array): Uint8Array | null {
    try {
      return this.pushInner(frame);
    } catch (e) {
      this.reset();
      throw e;
    }
  }

  reset() {
    this.buf = null;
    this.filled = 0;
    this.seq = 0;
  }

  private pushInner(frame: Uint8Array): Uint8Array | null {
    if (frame.length < 2) throw new Error('frame too short');
    const flags = frame[0]!;
    const last = (flags & LAST) !== 0;
    let payload: Uint8Array;
    if (flags & FIRST) {
      if (frame.length < 5) throw new Error('first frame too short');
      const len = new DataView(frame.buffer, frame.byteOffset, frame.byteLength).getUint32(1);
      if (len > MAX_MESSAGE) throw new Error('declared length too large');
      this.buf = new Uint8Array(len);
      this.filled = 0;
      this.seq = 0;
      payload = frame.subarray(5);
    } else {
      if (!this.buf) throw new Error('continuation without first frame');
      const expect = this.seq === 255 ? 1 : this.seq + 1;
      if (frame[1] !== expect) throw new Error(`sequence gap: got ${frame[1]}, want ${expect}`);
      this.seq = expect;
      payload = frame.subarray(2);
    }
    const buf = this.buf!;
    if (this.filled + payload.length > buf.length) throw new Error('more payload than declared');
    buf.set(payload, this.filled);
    this.filled += payload.length;
    if (last) {
      if (this.filled !== buf.length) throw new Error('last frame before message complete');
      const msg = buf;
      this.reset();
      return msg;
    }
    if (this.filled === buf.length) throw new Error('message complete but last flag missing');
    return null;
  }
}
