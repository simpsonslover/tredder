// Cross-implementation test: the app's Noise must reproduce, byte for byte, the vectors the
// firmware's C implementation wrote (firmware/tools/noise_vectors.py -> protocol/noise_vectors.json).
import { hexToBytes, utf8ToBytes, bytesToHex } from '@noble/hashes/utils.js';

import vectors from '../../../../protocol/noise_vectors.json';
import { pairCode, publicKeyOf, XXInitiator } from '../noise';

const hx = (s: string) => hexToBytes(s);
const V = vectors as Omit<typeof vectors, 'toResponder' | 'toInitiator'> & { toResponder: [string, string]; toInitiator: [string] };

describe('Noise XX (vs firmware vectors)', () => {
  it('reproduces the handshake, the pairing code and the transport messages', () => {
    const i = new XXInitiator(hx(V.initiatorStatic), hx(V.initiatorEphemeral));
    expect(bytesToHex(i.msg1())).toBe(V.msg1);
    i.readMsg2(hx(V.msg2));
    expect(bytesToHex(i.msg3())).toBe(V.msg3);
    const s = i.split();
    expect(bytesToHex(s.hash)).toBe(V.handshakeHash);
    expect(bytesToHex(s.remoteStatic)).toBe(bytesToHex(publicKeyOf(hx(V.responderStatic))));
    expect(pairCode(s.hash)).toBe(String(V.pairCode).padStart(6, '0'));
    expect(bytesToHex(s.send.seal(utf8ToBytes(V.toResponder[0])))).toBe(V.toResponder0);
    expect(bytesToHex(s.send.seal(utf8ToBytes(V.toResponder[1])))).toBe(V.toResponder1);
    expect(new TextDecoder().decode(s.recv.open(hx(V.toInitiator0)))).toBe(V.toInitiator[0]);
  });

  it('rejects a tampered msg2 and a tampered transport message', () => {
    const bad = hx(V.msg2);
    bad[40] = bad[40]! ^ 1; // inside the encrypted static key
    const i = new XXInitiator(hx(V.initiatorStatic), hx(V.initiatorEphemeral));
    i.msg1();
    expect(() => i.readMsg2(bad)).toThrow();
    expect(() => i.msg3()).toThrow();

    const j = new XXInitiator(hx(V.initiatorStatic), hx(V.initiatorEphemeral));
    j.msg1();
    j.readMsg2(hx(V.msg2));
    j.msg3();
    const s = j.split();
    const t = hx(V.toInitiator0);
    t[0] = t[0]! ^ 1;
    expect(() => s.recv.open(t)).toThrow();
    expect(new TextDecoder().decode(s.recv.open(hx(V.toInitiator0)))).toBe(V.toInitiator[0]); // counter not consumed
  });
});
