"""
Talk to a tredder over real Bluetooth from a PC: scan, connect, Noise XX handshake
(an independent Python implementation, checked against protocol/noise_vectors.json first),
then send requests.

    pip install bleak cbor2 cryptography
    python firmware/tools/ble_probe.py              # get-info
    python firmware/tools/ble_probe.py --pair       # also get-accounts: pairing prompt on the device

Development tool. The PC's static key is random per run, so the device sees a new app each time.
"""
import argparse
import asyncio
import hashlib
import hmac
import json
import os
import struct
import sys

import cbor2
from bleak import BleakClient, BleakScanner
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

SERVICE = "7a3f0001-5c1e-4c6b-9f2e-6b7d2a1e0b10"
RX = "7a3f0002-5c1e-4c6b-9f2e-6b7d2a1e0b10"
TX = "7a3f0003-5c1e-4c6b-9f2e-6b7d2a1e0b10"
VECTORS = os.path.join(os.path.dirname(__file__), "..", "..", "protocol", "noise_vectors.json")


# ---------------------------------------------------------------- Noise XX initiator
def pub(priv: bytes) -> bytes:
    return X25519PrivateKey.from_private_bytes(priv).public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)


def dh(priv: bytes, peer: bytes) -> bytes:
    return X25519PrivateKey.from_private_bytes(priv).exchange(X25519PublicKey.from_public_bytes(peer))


def hkdf2(ck: bytes, ikm: bytes):
    prk = hmac.new(ck, ikm, hashlib.sha256).digest()
    o1 = hmac.new(prk, b"\x01", hashlib.sha256).digest()
    return o1, hmac.new(prk, o1 + b"\x02", hashlib.sha256).digest()


def nonce(n: int) -> bytes:
    return b"\0" * 4 + struct.pack("<Q", n)


class XX:
    def __init__(self, s: bytes, e: bytes):
        self.h = b"Noise_XX_25519_ChaChaPoly_SHA256"
        self.ck, self.k, self.n = self.h, None, 0
        self.mix_hash(b"tredder/1")
        self.s, self.e = s, e

    def mix_hash(self, d):
        self.h = hashlib.sha256(self.h + d).digest()

    def mix_key(self, ikm):
        self.ck, self.k = hkdf2(self.ck, ikm)
        self.n = 0

    def enc(self, p):
        c = ChaCha20Poly1305(self.k).encrypt(nonce(self.n), p, self.h) if self.k else p
        if self.k:
            self.n += 1
        self.mix_hash(c)
        return c

    def dec(self, c):
        p = ChaCha20Poly1305(self.k).decrypt(nonce(self.n), c, self.h) if self.k else c
        if self.k:
            self.n += 1
        self.mix_hash(c)
        return p

    def msg1(self):
        ep = pub(self.e)
        self.mix_hash(ep)
        return ep + self.enc(b"")

    def read_msg2(self, m):
        assert len(m) == 96, f"msg2 is {len(m)} bytes"
        self.re = m[:32]
        self.mix_hash(self.re)
        self.mix_key(dh(self.e, self.re))
        self.rs = self.dec(m[32:80])
        self.mix_key(dh(self.e, self.rs))
        self.dec(m[80:])

    def msg3(self):
        c = self.enc(pub(self.s))
        self.mix_key(dh(self.s, self.re))
        return c + self.enc(b"")

    def split(self):
        k1, k2 = hkdf2(self.ck, b"")
        return Cipher(k1), Cipher(k2), self.h, self.rs


class Cipher:
    def __init__(self, k):
        self.a, self.n = ChaCha20Poly1305(k), 0

    def seal(self, p):
        c = self.a.encrypt(nonce(self.n), p, None)
        self.n += 1
        return c

    def open(self, c):
        p = self.a.decrypt(nonce(self.n), c, None)
        self.n += 1
        return p


def pair_code(h):
    d = hashlib.sha256(b"tredder pairing code" + h).digest()
    return f"{struct.unpack('>I', d[:4])[0] % 1000000:06d}"


def check_vectors():
    v = json.load(open(VECTORS))
    x = XX(bytes.fromhex(v["initiatorStatic"]), bytes.fromhex(v["initiatorEphemeral"]))
    assert x.msg1().hex() == v["msg1"]
    x.read_msg2(bytes.fromhex(v["msg2"]))
    assert x.msg3().hex() == v["msg3"]
    send, recv, h, _ = x.split()
    assert h.hex() == v["handshakeHash"] and pair_code(h) == f"{v['pairCode']:06d}"
    assert send.seal(v["toResponder"][0].encode()).hex() == v["toResponder0"]
    print("python Noise matches the firmware vectors")


# ---------------------------------------------------------------- framing (protocol section 2)
def frames(msg: bytes, fs: int):
    first = min(fs - 5, len(msg))
    out = [bytes([0x80 | (0x40 if first == len(msg) else 0)]) + struct.pack(">I", len(msg)) + msg[:first]]
    off, seq = first, 1
    while off < len(msg):
        k = min(fs - 2, len(msg) - off)
        out.append(bytes([0x40 if off + k == len(msg) else 0, seq]) + msg[off:off + k])
        off += k
        seq = 1 if seq == 255 else seq + 1
    return out


class Reassembler:
    def __init__(self):
        self.buf, self.len = None, 0

    def push(self, f: bytes):
        if f[0] & 0x80:
            self.len = struct.unpack(">I", f[1:5])[0]
            self.buf = bytearray(f[5:])
        elif self.buf is not None:
            self.buf += f[2:]
        if f[0] & 0x40 and self.buf is not None and len(self.buf) == self.len:
            m, self.buf = bytes(self.buf), None
            return m
        return None


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pair", action="store_true", help="also request accounts (needs pairing on the device)")
    args = ap.parse_args()
    check_vectors()

    print("scanning ...")
    dev = await BleakScanner.find_device_by_filter(lambda d, ad: SERVICE in [u.lower() for u in ad.service_uuids], timeout=15)
    if not dev:
        sys.exit("no tredder found (is it on and advertising?)")
    print(f"found {dev.name} {dev.address}")
    inbox: asyncio.Queue = asyncio.Queue()
    rx = Reassembler()

    def on_notify(_, data: bytearray):
        m = rx.push(bytes(data))
        if m is not None:
            inbox.put_nowait(m)

    async with BleakClient(dev) as c:
        fs = max(20, c.mtu_size - 3)
        print(f"connected, MTU {c.mtu_size} -> frames of {fs} bytes")
        await c.start_notify(TX, on_notify)

        async def send(m):
            for f in frames(m, fs):
                await c.write_gatt_char(RX, f, response=True)

        x = XX(X25519PrivateKey.generate().private_bytes_raw(), X25519PrivateKey.generate().private_bytes_raw())
        await send(x.msg1())
        x.read_msg2(await asyncio.wait_for(inbox.get(), 10))
        await send(x.msg3())
        tx, rcv, h, dev_static = x.split()
        print(f"secure channel open; device key {dev_static.hex()[:16]}...; pairing code {pair_code(h)}")

        async def request(req, timeout=10.0):
            await send(tx.seal(cbor2.dumps(req)))
            while True:
                m = cbor2.loads(rcv.open(await asyncio.wait_for(inbox.get(), timeout)))
                if m.get("type") == "event":
                    print("  event:", m)
                    continue
                return m

        print("get-info ->", await request({"id": 1, "type": "get-info"}))
        if args.pair:
            print(f"get-accounts: the device should now ask to pair with code {pair_code(h)} ...")
            r = await request({"id": 2, "type": "get-accounts", "wallet": "active", "net": "test", "scheme": "btc-p2wpkh",
                               "accounts": [0]}, timeout=300)
            print("get-accounts ->", r)


if __name__ == "__main__":
    asyncio.run(main())
