"""
Screenshots of the device UI from the PC emulator (same firmware code as the board), for
design review. Starts tredder-emu headless with a fresh in-memory state, walks through the
main screens with the emulated buttons and saves PNGs.

    python firmware/tools/device_shots.py [OUT_DIR]
"""
import json
import os
import socket
import struct
import subprocess
import sys
import time
import zlib

FW = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
EMU = os.path.join(FW, "build-emu", "tredder-emu.exe")
PORT = 9555


def bmp_to_png(bmp_path, png_path):
    d = open(bmp_path, "rb").read()
    off, w, h = struct.unpack_from("<I", d, 10)[0], struct.unpack_from("<i", d, 18)[0], struct.unpack_from("<i", d, 22)[0]
    row = (w * 3 + 3) & ~3
    raw = b""
    for y in range(h):
        src = d[off + (h - 1 - y) * row: off + (h - 1 - y) * row + w * 3]
        raw += b"\0" + bytes(b for i in range(0, len(src), 3) for b in (src[i + 2], src[i + 1], src[i]))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(png_path, "wb").write(png)
    os.remove(bmp_path)


TEST_PHRASE = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about"


class Emu:
    def __init__(self, out, extra=()):
        self.out = out
        self.p = subprocess.Popen([EMU, "--ephemeral", "--headless", "--debug-port", str(PORT), "--port", "8799", "--sleep-ms", "0", "--battery", "72", *extra],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                self.s = socket.create_connection(("127.0.0.1", PORT), timeout=30)
                break
            except OSError:
                time.sleep(0.1)
        self.f = self.s.makefile("rw", newline="\n")

    def cmd(self, c):
        self.f.write(c + "\n")
        self.f.flush()
        return self.f.readline().strip()

    def screen(self):
        return json.loads(self.cmd("screen"))

    def press(self, *bs):
        for b in bs:
            assert self.cmd(f"press {b}") == "ok"

    def shot(self, name):
        time.sleep(0.25)
        bmp = os.path.join(self.out, name + ".bmp")
        assert self.cmd("shot " + bmp) == "ok", name
        bmp_to_png(bmp, os.path.join(self.out, name + ".png"))
        print("saved", name)

    def close(self):
        self.p.kill()


def main():
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(FW, "build-emu", "shots"))
    os.makedirs(out, exist_ok=True)
    e = Emu(out)
    try:
        time.sleep(1.0)
        e.shot("01_welcome")
        e.press("ok")                      # Create new wallet
        e.shot("02_create_intro")
        for i in range(12):                 # walk through intro pages until the word list shows
            s = e.screen()
            if s["kind"] == "pages" and any("1." in l or l.strip().startswith("1 ") for l in s["lines"]):
                break
            e.press("ok" if s["page"] + 1 >= s["npages"] else "down")
        e.shot("03_words")
    finally:
        e.close()
    time.sleep(0.5)
    # a device with the public test wallet, locked
    e = Emu(out, ["--seed", TEST_PHRASE, "--pin", "1234"])
    try:
        time.sleep(1.0)
        e.shot("04_locked")
        e.press("ok")                      # Unlock -> PIN pad
        e.press("down")
        e.shot("05_pin")
        e.cmd("type 1234")
        e.cmd("enter")
        time.sleep(1.5)                    # PIN key derivation
        e.shot("06_home")
        for _ in range(3):                 # to the last item (Wipe device)
            e.press("down")
        e.shot("07_home_scrolled")
        e.press("ok")
        e.shot("08_danger")
    finally:
        e.close()


if __name__ == "__main__":
    main()
