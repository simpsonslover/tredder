# Phase 2: firmware (RP2350, Pico C SDK)

## Layout

```
firmware/
  core/            portable wallet logic, no hardware access; unit-tested on the PC
    include/wallet/  status, secure, entropy, mnemonic, schemes, address, keys,
                     btc (PSBT), evm, sol, cbor, review (what the user must see)
    src/
  app/             the device application on top of a small HAL (hal.h, se.h):
                     ui (screens + flows), vault (PIN + encrypted seed), proto (protocol v1),
                     gfx (ST7789 driver + fonts), rng (HMAC-DRBG), firmware (main loop)
  platform/rp2350/ Pico SDK code: main, platform hooks, generated board_pins.h
  platform/emu/    the firmware on emulated hardware as a Windows program (README inside)
  third_party/     audited crypto, vendored UNMODIFIED with provenance (VENDORED.md)
    trezor-crypto/   Trezor core/v2.13.0: BIP39/32, secp256k1, ed25519, hashes, AES-GCM, ChaCha20-Poly1305, SLIP-39
    secp256k1/       bitcoin-core libsecp256k1 v0.8.0: Taproot tweak, BIP340 Schnorr, ECDSA recovery
  tests/host/      unit tests + generated vectors.h
  tools/
    host_test.py     build + run the unit tests with MSYS2 GCC (no SDK needed)
    gen_vectors.py   regenerate vectors.h (official vectors + independent reference, bip_utils)
    gen_board_pins.py  board_pins.h from hardware/tools/design.py (one source of truth for pins)
    gen_font.py      display fonts (DejaVu) -> app/src/font_data.c
    build_emu.py     build firmware/build-emu/tredder-emu.exe
  CMakeLists.txt   RP2350 build (needs Pico SDK 2.x toolchain)
```

## Milestones

| | Milestone | Status |
|---|---|---|
| **M1** | Wallet core: entropy mixer, BIP39 (generate/import 12–24 words, autocomplete), derivation and addresses for every scheme, memory hygiene, host test harness, on-target self-test image | **done**: 8 347 host checks pass |
| M2 | Secure storage + PIN: key hierarchy (below), SE050 driver (Platform SCP03), attempt counter and wipe, encrypted seed blob, HMAC-DRBG, randomized PIN pad logic | **logic done + emulator-tested** (`app/src/vault.c`, `rng.c`); SE050 I2C driver for the board still to do |
| M3 | Drivers + UI: ST7789, buttons, power/charger, screens (PIN pad, word picker, review, confirm), idle lock; PC-side UI simulator | **UI + ST7789 driver done, emulator-tested** (`app/src/ui.c`, `gfx.c`); RP2350 GPIO/SPI/flash HAL + charger still to do |
| M4 | Signing (testnets first): BTC PSBT (P2WPKH/P2TR/P2SH-P2WPKH/P2PKH), EVM EIP-1559 + clear signing of ERC-20 transfer/approve (unlimited-approval warning, blind signing off), Solana system/SPL transfers | **done, emulator-tested** against the app's own code (`core/src/btc.c`, `evm.c`, `sol.c`) |
| M5 | BLE: LE Secure Connections numeric comparison, single bonded phone, authenticated + encrypted app channel (Noise, from trezor-crypto), request/approve protocol, auto-lock on disconnect | protocol + approvals + auto-lock done (`app/src/proto.c`); BLE (CYW43) and Noise still to do |
| M6 | Backups: microSD AES-256-GCM + Argon2id with device-generated 12-word backup password, versioned file with checksum; SLIP-39 shares | |
| M7 | Import + sweep (device side): scheme scan list, sweep plan display, PIN + confirm, resumable state | |
| M8 | Secure boot, signed updates, anti-rollback, OTP lockdown (SWD off, glitch detectors), on a sacrificial board first | |

## Verified so far (M1)

`python firmware/tools/host_test.py` builds both libraries plus the core and runs **8 347 checks, 0 failures**. All of our code compiles with `-Wall -Wextra -Werror -Wshadow -Wvla`.

- **BIP39:** all 24 official vectors (entropy → words → seed with passphrase "TREZOR").
- **Phrase lengths:** import of 12/15/18/21/24-word phrases. The error message names the first unknown word ("word 8 is not a BIP39 word"), or reports a bad checksum or a wrong word count.
- **Checksum strength:** single-word typos are caught at the rate the checksum allows, 1 − 2^(−words/3).
- **Word picker:** every word is unique after 4 letters, and the next-letter mask only offers letters that lead to a real word.
- **SLIP-10 ed25519:** all 12 official vectors.
- **Official addresses:** BIP84 and BIP86 (mainnet) and BIP49 (testnet) official addresses.
- **1 980 differential vectors** against an independent implementation (bip_utils), with paths written from each wallet's documentation, not from our code. They cover:
  - every scheme (BTC legacy, nested, native, and Taproot; EVM standard and Ledger Live; Solana standard, BIP44, root, and CLI seed)
  - mainnet and testnet
  - accounts 0/1/7, change, and index
  - 8 phrases of all lengths
  - 3 passphrases
- **Entropy mixer:**
  - a known-answer test
  - refuses output unless both TRNG and SE provide ≥ 256 bits
  - stuck or biased TRNG trips the latched SP 800-90B health tests
  - constant SE output is rejected
  - the pool is wiped after use

## Bugs and decisions found while building M1

1. **trezor-crypto's `mnemonic_check()` only accepts 12/18/24 words**, because Trezor devices only create those. Your spec requires importing 15- and 21-word phrases, which the differential tests caught (440 failures). `core/src/mnemonic.c` now checks the BIP39 checksum for every length using the library's own `mnemonic_to_bits()` and SHA-256.
2. **`hdnode_fill_public_key()` returns 0 on success**, unlike its siblings. Documented at the call site.
3. **Library caches disabled:** `USE_BIP39_CACHE=0` and `USE_BIP32_CACHE=0`. The defaults keep seeds and private keys in static RAM. Our code refuses to compile if either is on.
4. **`mnemonic_from_data()` returns a static buffer:** we copy the words out and call `mnemonic_clear()` immediately.
5. **Fault handler:** trezor-crypto's default `tc_fault_handler` is a no-op ("not suitable for production"). Ours halts. In M2 it also wipes secrets.
6. **Trade-off: passphrases are printable ASCII only (max 100 characters).** That's what the buttons can enter, and for ASCII the BIP39 Unicode normalisation is a no-op. So results match every other wallet without shipping a Unicode library. A passphrase created elsewhere with accents or emoji can't be entered on this device. The device says so rather than silently deriving a different wallet.
7. **Taproot uses libsecp256k1** (Bitcoin Core's library), not hand-written elliptic-curve code.

## Security design for M2 (key hierarchy, to verify against NXP AN12413 / AN12514 and Nitrokey 3's SE050 code)

```
PIN (entered on device, randomized pad)
  └─ stretched = PBKDF2-HMAC-SHA256(PIN, salt = device_salt, many iterations)   (slow down brute force)
mcu_secret: 32 B random, in RP2350 OTP, page-locked to secure code
se_secret:  32 B in an SE050 binary object readable only in a session authenticated
            with an AES key derived from `stretched` (auth object, MaxAttempts = 10)
bus:        Platform SCP03 (encrypted + MACed I2C), keys rotated from NXP's defaults,
            stored in RP2350 OTP
KEK  = HKDF-SHA256(stretched || se_secret || mcu_secret)
seed_blob (flash) = ChaCha20-Poly1305(KEK, nonce, seed || passphrase-flag || version)
```

- **Counter before check:** the attempt counter is incremented *before* the PIN is checked, and reset only on success. Cutting power mid-attempt still costs an attempt.
- **Wipe after 10 failures:** the SE050 locks the PIN key after 10 failures, so `se_secret`, and with it the seed, is gone for good. Firmware also erases the blob.
- **What an attacker would need:** the PIN, *plus* defeating the SE counter, *plus* reading RP2350 OTP. Desoldering the flash yields only ciphertext.
- **Increasing delays** between attempts are enforced by firmware. The *limit* is enforced by the SE.

## Rules carried over from the hardware simulation (hardware/README.md §9)

1. Drive SPI0 pins low before switching the SD card off, and wait ≥ 150 ms before re-powering it.
2. The SE050 has no reset pin: reset it by switching Q2 off (GP11 high) for at least 40 ms (SE_VCC takes ~35 ms to fall below 0.3 V, hardware sim `se_power`). Leave GP8/GP9 without internal pull-ups and never drive them high (open-drain only): the bus pull-ups go to the switched SE supply, and SDA driven high would back-power an unpowered SE to ~1.5 V.
3. Calibrate the VBAT ADC gain per device. GP28 must use `adc_gpio_init` (no pulls).
4. Read CHG_STAT (GP5) as an input with no pull.

## Running

**Host tests** (works now): `python firmware/tools/host_test.py` (add `--clean` to rebuild everything).

**Regenerate vectors** (only when adding schemes):
1. `pip install bip_utils` in a Python ≤ 3.12 venv.
2. Download the source files listed in `tools/gen_vectors.py`.
3. Run `python tools/gen_vectors.py <dir>`.

**On-target self-test** (needs the Pico toolchain):
1. Install the *Raspberry Pi Pico* VS Code extension. It installs SDK 2.x, arm-none-eabi-gcc, CMake, Ninja and picotool.
2. Build with `cmake -S firmware -B firmware/build -G Ninja && cmake --build firmware/build`.
3. Hold BOOTSEL and drag `firmware/build/wallet.uf2` onto the Pico 2 W.
4. Open the USB serial port. It prints PASS/FAIL for each scheme plus timings.

This works on a bare Pico 2 W; the custom board isn't needed.
