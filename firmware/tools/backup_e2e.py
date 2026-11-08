"""
End-to-end test of the microSD backup (milestone M6) on the PC emulator: the real firmware,
driven only through its four buttons (the debug link's robot finger), on an emulated card.

  run 1: a device with the public test wallet backs up to the card (reads the password off the
         screen, passes the check)
  run 2: a NEW device (flash and secure element wiped, same card) restores it:
         no card -> message; wrong password -> "Wrong password", retry; a password typo;
         then the right password -> same wallet (BIP32 fingerprint compared)

    python firmware/tools/backup_e2e.py [--shots DIR]
"""
import argparse
import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(__file__))
from device_shots import EMU, Emu, TEST_PHRASE  # noqa: E402

TEST_FPR = "73c5da0a"  # BIP32 master fingerprint of the "abandon ... about" test phrase


class Dev(Emu):
    def __init__(self, state, shots, extra=()):
        self.out = shots
        self.n = 0
        import socket
        import subprocess
        self.p = subprocess.Popen([EMU, "--state", state, "--headless", "--debug-port", "9556", "--port", "8798",
                                   "--sleep-ms", "0", "--battery", "72", "--pin-delay-ms", "0", *extra],
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            try:
                self.s = socket.create_connection(("127.0.0.1", 9556), timeout=60)
                break
            except OSError:
                time.sleep(0.1)
        self.f = self.s.makefile("rw", newline="\n")
        time.sleep(1.0)

    def snap(self, name):
        if self.out:
            self.n += 1
            self.shot(f"{self.n:02d}_{name}")

    def wait_title(self, want, timeout=30):
        end = time.time() + timeout
        wants = want if isinstance(want, tuple) else (want,)
        while time.time() < end:
            s = self.screen()
            if any(w in s["title"] or any(w in l for l in s["lines"]) for w in wants):
                return s
            time.sleep(0.2)
        raise AssertionError(f"waited for {want!r}, screen is {self.screen()}")

    def select(self, text):
        s = self.screen()
        idx = next(i for i, it in enumerate(s["items"]) if text in it)
        while self.screen()["cursor"] != idx:
            self.press("down")
        self.press("ok")

    def pages_to_end(self):
        """Through a multi-page screen and OK on the last page; returns all lines seen."""
        seen = []
        s = self.screen()
        assert s["kind"] == "pages", s
        while True:
            s = self.screen()
            seen += s["lines"]
            if s["page"] + 1 >= s["npages"]:
                self.press("ok")
                return seen
            self.press("down")

    def type_pin(self, pin):
        self.cmd("type " + pin)
        self.cmd("enter")

    def type_word(self, w):
        # BIP39 words are unique after 4 letters (the picker then takes the word itself);
        # shorter words need the explicit "✓ word" choice.
        if len(w) >= 4:
            self.cmd("type " + w[:4])
        else:
            self.cmd("type " + w)
            self.cmd("enter")


def check(cond, what):
    print(("  ok    " if cond else "  FAIL  ") + what)
    if not cond:
        raise SystemExit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", help="save screenshots here")
    a = ap.parse_args()
    shots = os.path.abspath(a.shots) if a.shots else None
    if shots:
        os.makedirs(shots, exist_ok=True)
    state = tempfile.mkdtemp(prefix="tredder-e2e-")
    try:
        print("run 1: back up the test wallet")
        d = Dev(state, shots, ["--seed", TEST_PHRASE, "--pin", "1234"])
        try:
            d.wait_title("Locked")
            d.press("ok")
            d.type_pin("1234")
            d.wait_title("Unlocked")
            check(d.cmd("fpr") == TEST_FPR, "test wallet loaded (fingerprint " + TEST_FPR + ")")
            d.snap("home")
            d.select("Back up to microSD")
            d.type_pin("1234")
            d.wait_title("Back up to microSD")
            d.snap("backup_intro")
            d.pages_to_end()
            d.wait_title("Card OK")
            d.pages_to_end()
            d.wait_title("Backup password")
            d.snap("password")
            lines = d.pages_to_end()
            pw = {}
            for l in lines:
                l = l.strip()
                if len(l) > 3 and l.split(".")[0].isdigit():
                    pw[int(l.split(".")[0])] = l.split(".", 1)[1].strip()
            check(sorted(pw) == list(range(1, 13)), "12 password words shown")
            for k in range(3):
                s = d.wait_title(f"Check {k + 1} of 3")
                if k == 0:
                    d.snap("quiz")
                n = int(s["lines"][0].split("#")[1].rstrip("?"))
                d.select(pw[n])
            s = d.wait_title("Backup saved", timeout=60)
            d.snap("saved")
            name = next(l.strip() for l in s["lines"] if l.strip().startswith("tredder-"))
            check(name.endswith(".trd") and len(name) == 18, f"backup written and verified: {name}")
            d.press("ok")
        finally:
            d.close()
        time.sleep(0.5)
        for f in os.listdir(state):  # a brand-new device: only the card survives
            if f != "sdcard.img":
                os.remove(os.path.join(state, f))

        print("run 2: restore on a new device")
        d = Dev(state, shots)
        try:
            d.wait_title("Welcome")
            check(d.screen()["initialized"] is False, "new device has no wallet")
            check(d.cmd("sd out") == "ok", "card removed")
            d.select("Restore microSD backup")
            d.wait_title("No microSD card")
            d.snap("no_card")
            d.press("ok")
            check(d.cmd("sd in") == "ok", "card inserted")
            d.wait_title("Welcome")
            d.select("Restore microSD backup")
            d.wait_title("Enter the 12-word")
            d.snap("restore_intro")
            d.pages_to_end()
            # 1. a wrong but valid password (the test phrase itself: right checksum, wrong key)
            for w in TEST_PHRASE.split():
                d.type_word(w)
            s = d.wait_title("Review password")
            d.select("Password is correct")
            d.wait_title("Wrong password", timeout=60)
            d.snap("wrong_password")
            check(d.cmd("fpr") == "error locked", "nothing restored after a wrong password")
            d.select("Enter it again")
            # 2. a typo: word 12 replaced, caught by the checksum on the review screen
            words = [pw[i] for i in range(1, 13)]
            for w in words[:11]:
                d.type_word(w)
            d.type_word("zoo" if words[11] != "zoo" else "zone")
            s = d.wait_title(("Checksum wrong!", "Review password"))
            if s["title"] == "Checksum wrong!":
                check(True, "password typo caught by the checksum before decrypting")
                d.snap("typo")
                d.select("12.")
                d.type_word(words[11])
                d.wait_title("Review password")
            else:
                print("  note  the typo happened to have a valid checksum (1 in 16); fixing it")
                d.select("12.")
                d.type_word(words[11])
                d.wait_title("Review password")
            d.select("Password is correct")
            d.wait_title("Backup OK", timeout=60)
            d.snap("backup_ok")
            check(any("12 words" in l for l in d.screen()["lines"]), "decrypted: 12-word wallet")
            d.pages_to_end()
            d.type_pin("5678")
            d.type_pin("5678")
            d.wait_title("Restored", timeout=30)
            d.snap("restored")
            d.press("ok")
            d.wait_title("Unlocked")
            check(d.cmd("fpr") == TEST_FPR, "restored wallet is the same wallet (fingerprint " + TEST_FPR + ")")
        finally:
            d.close()
        print("backup e2e: ALL PASS")
    finally:
        time.sleep(0.3)
        shutil.rmtree(state, ignore_errors=True)


if __name__ == "__main__":
    main()
