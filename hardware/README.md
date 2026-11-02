# Phase 1: main board (rev A)

KiCad project: `wallet.kicad_pro`. The schematic is saved in KiCad 8 format and opens in KiCad 8, 9, and 10. The PCB file was written by KiCad 10.0.7, which is installed on this machine, so **open it with KiCad 10**. KiCad 8 can't open boards saved by newer versions.

Everything is generated from **[tools/design.py](tools/design.py)**, which defines parts, pins, nets, and part numbers. The schematic, PCB netlist, and BOM come from that one file, so they can't drift apart.

| File | What it is |
|---|---|
| `wallet.kicad_sch` | Schematic (label-style, one A2 sheet, grouped by function) |
| `wallet.kicad_pcb` | 4-layer, 85.6 × 54 mm (credit card) PCB |
| `wallet.kicad_sym`, `wallet.pretty/` | Project symbols and two custom footprints |
| `bom/wallet_bom_full.csv` | Full BOM: MPN, LCSC, notes, and a **VERIFY** column |
| `bom/wallet_bom_jlc.csv` | JLCPCB assembly BOM (parts JLC can place) |
| `erc.rpt`, `drc.rpt` | Last ERC/DRC results |
| `tools/build.ps1` | Regenerates everything and reruns ERC/DRC |
| `sim/` | Circuit simulation of the whole board (ngspice), 18 tests: see §9 and [sim/README.md](sim/README.md) |
| `tools/sync_values.py` | Pushes value-only changes from `design.py` into the routed PCB without touching copper |

---

## 1. Block diagram

```
 USB-C ──F1──U1(ESD)──R3/R4(0Ω, DNP=charge-only)──► Pico TP2/TP3 (USB D-/D+)
   │ VBUS
   ├────────► Pico VBUS ──(Schottky on module)──► VSYS ◄──Q1 (load-share PFET)◄── SW1-A ◄── BT1 LiPo (J2)
   └──► U3 MCP73831 charger ───────────────────────────────────────────────────────┘ VBAT
                                                    SW1-B: 3V3_EN → GND (hard off, even on USB)

 Pico 2 W (RP2350 + CYW43439 BLE)
   I2C0  GP8/GP9  ──────► U2 OPTIGA Trust M V3   (VCC switched by Q2, RST on GP10)
   SPI0  GP0-3    ──────► J3 microSD            (VDD switched by Q3, soft-start, DET on GP6)
   SPI1  GP26/27  ──────► J4 2.0" ST7789 module (CS GP21, DC GP22, RST GP20, BL via Q4 on GP19,
                          module VCC switched by Q5 on GP18: unpowered in sleep)
   GP12-15        ──────► Back / Confirm / Up / Down (10k pull-ups + RC debounce)
   GP28 (ADC2)    ◄────── VBAT divider (after power switch, no drain when off)
   GP5            ◄────── charger STAT (5V→3.3V divider)
   SWD pads D1-D3 ──────► Tag-Connect TC2030 footprint (no connector populated)
   GP16/17 UART0  ──────► test pads (prototype console)
```

## 2. Pin map

| GPIO | Function | | GPIO | Function |
|---|---|---|---|---|
| GP0 | SD MISO (SPI0 RX) | | GP14 | BTN UP |
| GP1 | SD CS (SPI0 CSn) | | GP15 | BTN DOWN |
| GP2 | SD SCK | | GP16 | UART0 TX (test pad) |
| GP3 | SD MOSI | | GP17 | UART0 RX (test pad) |
| GP4 | spare (TP7) | | GP18 | LCD power enable (active low, Q5; TP8) |
| GP5 | CHG_STAT | | GP19 | LCD backlight (active low) |
| GP6 | SD card detect | | GP20 | LCD RST |
| GP7 | SD power enable (active low) | | GP21 | LCD CS |
| GP8 | SE SDA (I2C0) | | GP22 | LCD DC |
| GP9 | SE SCL (I2C0) | | GP26 | LCD SCK (SPI1) |
| GP10 | SE RST (active low) | | GP27 | LCD MOSI (SPI1) |
| GP11 | SE power enable (active low) | | GP28 | VBAT sense (ADC2) |
| GP12 | BTN BACK | | GP23-25, 29 | used inside the module by the radio |
| GP13 | BTN CONFIRM | | | |

All five switch controls (Q2 SE, Q3 SD, Q4 backlight, Q5 display power, and SW1) default to **off** when the MCU is in reset or unprogrammed.

## 3. Secure element: OPTIGA Trust M V3 (recommended) vs ATECC608B

**Recommendation: Infineon OPTIGA Trust M V3, `SLS32AIA010MKUSON10XTMA2`.**

| | OPTIGA Trust M V3 | ATECC608B |
|---|---|---|
| Certification | Common Criteria EAL6+ (high), whole chip | No CC certification for the device |
| MCU↔SE link | **Shielded Connection** (AES-encrypted, authenticated I2C) | Encrypted reads possible, but the bus protocol is mostly plaintext |
| PIN-attempt counter in the SE | Monotonic counters can gate access to a data object, so the key becomes unreadable after N failures | Possible with monotonic counter + slot config, but it's a fiddlier pattern, and the full datasheet is under NDA |
| Reference for exactly our use | **Trezor Safe 3/5 use this chip for PIN-gated secrets, and their integration code is open source** | Coldcard Mk3/Mk4 use the ATECC608 family (Mk4 adds a second SE) |
| Docs | Datasheet and host library (optiga-trust-m on GitHub) are public | Full datasheet NDA; CryptoAuthLib is public |
| Sourcing | Mouser/Digi-Key/Farnell (not LCSC basic) | LCSC/JLC stock it |

What you trade off: OPTIGA is harder to source and hand-solder (3 × 3 mm USON-10, 0.5 mm pitch, hot air recommended). Its config model also has a learning curve. We take that for the certification, encrypted bus, and an open reference implementation we can study rather than invent.

**Phase 2 key scheme (design target, following Trezor's pattern):**
`seed_key = KDF(PIN-stretched secret, SE-held secret released after PIN check, RP2350 OTP secret)`.
An attacker needs the PIN, **and** must defeat the SE counter, **and** must read the MCU's OTP. Stealing one chip isn't enough.

## 4. Security decisions in this board (and what each trades away)

| Decision | Security gain | Convenience lost |
|---|---|---|
| R3/R4 0 Ω links on USB D+/D- (DNP in production) | Charge-only USB removes the whole USB stack and BOOTSEL/PICOBOOT as attack surface | Firmware updates then come via a signed image on microSD instead of USB |
| SW1 pole B grounds `3V3_EN` | Hard off, even when USB is plugged in; nothing runs while it's "off" | Can't wake from USB; must flip the switch |
| SE power switched (Q2), default off | Lets firmware hard-reset the SE and keep it unpowered when locked | One more GPIO and some firmware sequencing |
| Display module power-gated (Q5), and sleep locks the wallet | Sleep current 14 mA → 2.6 mA (≈ 8 days on BT1 instead of 1.5). Waking always needs the PIN, so a device left asleep is locked | Waking takes a button press + PIN; the display re-initialises (~0.3 s) |
| SD on its own SPI bus, power-gated (Q3) | Untrusted removable media can't share a bus with the display, and is unpowered unless in a backup flow | Need to power-cycle the card in firmware |
| No test points on SE I2C | Makes passive probing slightly harder (Shielded Connection is the real defence) | Harder to debug the SE link (use a scope on the IC pins) |
| Tag-Connect SWD footprint, no header | Nothing to remove for production; covered by the case; SWD disabled in OTP anyway | Need a TC2030-IDC-NL cable (~$40) |
| External pull-ups on buttons | Immune to RP2350 erratum E9 (input pull-down latch) whatever the chip stepping | 8 passives |
| All parts on top, display module above them | Single-sided assembly is cheaper | Display must be removed to reach test pads |

## 5. Honest physical-security assessment

**What this design resists**
- **Lost or stolen device, casual or opportunistic attacker:** strong. Without the PIN, the seed is ciphertext in external flash. The SE wipes its secret after 10 tries, and that counter lives in the SE, not in the MCU.
- **Reading the flash chip** (desoldering the W25Q on the Pico module): gets ciphertext only. This is exactly why the seed must never be in flash in plain form.
- **Firmware replacement:** secure boot rejects unsigned images, and anti-rollback rejects old ones.
- **SWD debugging:** disabled permanently in OTP.
- **Sniffing the MCU↔SE bus:** defeated by Shielded Connection.

**What it does not fully resist (be aware)**
- **Fault injection on the RP2350 (voltage/EM/laser glitching).** In Raspberry Pi's 2024–25 RP2350 hacking challenge, researchers broke the A2 stepping with glitching, and one team extracted OTP secrets. The A4 stepping fixed several of these, and the glitch detectors help, but a well-equipped lab attacker with your device for days is **not** stopped by the RP2350 alone. That's why the SE secret is part of the key derivation: glitching the MCU alone then shouldn't be enough.
- **The SE itself:** EAL6+ resists most lab attacks within its certified scope, but no chip is unbreakable. A nation-state lab attack is out of scope.
- **Evil maid / supply-chain implant:** an attacker who opens the case, adds a keylogger on the buttons or a tap on the display SPI, and returns the device could capture the PIN or the seed shown during setup. Mitigations: the tamper-evident case (Phase 4), checking the seal every time, and a firmware attestation check against the SE (Phase 2). The board itself can't detect this when it's off; there's no battery-backed tamper mesh.
- **Wireless:** the CYW43439 runs a closed-source Infineon firmware blob loaded by the RP2350. A remote bug there is a real (if unlikely) risk. The BLE design treats the phone and the radio as untrusted (everything authenticated, confirm on device), but an air-gapped design such as SeedSigner or Coldcard with QR/SD is strictly safer. **This is the single biggest trade-off of putting Bluetooth in the device.**
- **Cold-boot / RAM remanence while unlocked:** the firmware wipes secrets after use, but an attacker who glitches a running, unlocked device has more options than one who gets a locked device.
- **$5 wrench attack:** use the BIP39 passphrase (hidden wallet) feature in Phase 2.

## 6. Check these before ordering ⚠

1. **OPTIGA footprint:** I built `Infineon_PG-USON-10-2_3x3mm_P0.5mm` from KiCad's USON-10 3×3 land pattern **without an exposed pad**. The pinout comes from datasheet rev 3.70 Table 6 (1 GND, 3 SDA, 8 SCL, 9 RST, 10 VCC, others NC), but I couldn't read the package drawing (Figure 6). **Compare the pads with Figure 6 and check whether there's a thermal/exposed pad.**
2. **Display module pinout and outline:** J4 assumes the common 2.0" 240×320 ST7789V module order **GND, VCC, SCL, SDA, RES, DC, CS, BLK** and a ~60 × 36 mm outline (F.Fab rectangle). Waveshare and others use different orders. Buy the module first, then edit `NETS` in `design.py` to match. The firmware drives the panel landscape (`MADCTL 0x60` in `firmware/app/src/gfx.c`), assuming the module's native top edge points away from J4. If the picture is upside down on your module, use `0xA0`.
3. **LiPo connector polarity:** pin 1 = BAT+. Many cheap packs are wired reversed for JST-PH. A reversed pack destroys the charger. Use a cell **with a protection circuit (PCM)**, ≤ 50 × 34 × 6 mm to sit behind the board, ≥ 500 mAh (charge current is set to 500 mA by R5 = 2 kΩ). The design uses **BT1 = 1S LiPo "503035" (5.0 × 30 × 35 mm, 500 mAh, PCM, JST-PH 2.0 lead)**: a board-only mechanical part on the bottom side (`tools/add_battery.py`, outline on B.Fab + 3D model), stuck behind the left half with 0.5 mm foam tape, clear of the J4 header pins and the antenna. It is not in the JLC BOM: buy it separately and **check the lead polarity before plugging in**.
4. **LCSC numbers marked VERIFY in `bom/wallet_bom_full.csv`:** MCP73831 (C424093), 4.7 µF 0805 (C1779), JST PH SMD (C295747), PTC fuse, DM3AT microSD, and the JS202011JCQN switch. Also the parts changed after simulation: 2.2 k (C4190), 470 Ω (C23179), 15 k (C22809), and 4.7 µF 0603 (C19666). I'm not confident of these LCSC IDs. Look each one up and check stock.
5. **Pico 2 W underside pads:** USB D+/D- (TP2/TP3) and SWD (D1–D3) are **under** the module. The custom footprint adds 0.8 mm plated holes there so you can feed solder from the back. Otherwise reflow the module (stencil and hot plate). Test continuity before powering.
6. **Pico 2 W stepping:** confirm the RP2350 is stepping **A4** or later (the chip marking or `chip_id` in the bootrom). A4 includes the security fixes from the hacking challenge.
7. **Don't plug in both USB ports.** The Pico's own micro-USB is still on the module, under the display. Its VBUS ties to the same net. The case should block it.
8. **Antenna area:** keep metal (battery, screws, display frame) away from the right-hand 10 mm of the board. The battery must sit behind the left part of the board only (Phase 4).
9. **DPDT switch pinout:** this assumes the C&K JS202011JCQN pins 2 and 5 are the commons. Confirm with the datasheet.
10. **microSD card-detect:** this assumes the DM3AT DET switch closes to DET_A (tied to GND) when a card is inserted. Confirm with the datasheet; firmware can invert it if not.
11. **Routing:** see section 7. Review every autorouted trace yourself; don't send an autorouted security device to fab unseen.

## 7. Layout status

- Placement, board outline, 4-layer stack, and GND/3V3 pours are done.
  - In1 is solid GND.
  - In2 is a +3V3 plane.
  - The outer layers are GND fills.
- The Pico antenna keepout is respected.
- **Routing: complete draft.** GND/+3V3 pads get generated stub+via fanout to the planes (`gen_pcb.py: fanout`). The remaining signals were autorouted with Freerouting 2.1 (`tools/route.py`) on F.Cu/B.Cu only.
- **DRC: 0 unconnected, 0 clearance/short errors, 0 schematic-parity issues. ERC: 0 errors, 0 warnings.**
- What's left in `drc.rpt`:
  - 2 × "items not allowed": paste openings inside the stock Pico footprint's own RF keepout. These are harmless and come from the library footprint.
  - 2 × "starved thermal": USB-C shell tabs have one thermal spoke on F.Cu. They're still connected through the other layers.
  - Silkscreen overlap warnings: cosmetic, and JLC clips silk off pads.
- ⚠ **`tools/build.ps1 -Fresh` regenerates placement and DISCARDS routing.** After the first route, make changes in KiCad (F8 = update from schematic) or re-run `route.py`.

**Recommended manual clean-up in KiCad (before ordering):**
1. Fatten the VBUS, VBAT, and VSYS paths (≥ 0.5 mm) and keep them short.
2. Route USB D+/D- as a pair (USB 1.1 full-speed is forgiving, but keep it tidy).
3. Make sure no trace runs under the antenna keepout.
4. Stitch GND vias around the board edge and the Pico.
5. Run DRC with zero errors, then generate Gerbers using JLC's KiCad guide.

## 8. Bring-up / test plan (do this before Phase 2)

Test in this order and stop at the first failure.

1. **Bare board:** check continuity GND↔+3V3, GND↔VBUS, and GND↔VBAT. None should be shorted.
2. **Power, without the Pico:** connect USB-C through a USB power meter. Check:
   - VBUS is ~5 V.
   - The charger LED is off with no battery.
   - Adding the battery makes the LED light, and VBAT climbs to 4.2 V then the LED goes off.
3. **Fit the Pico.** Check:
   - SW1 ON → 3.3 V on the 3V3 test pad.
   - SW1 OFF → 0 V, **even with USB connected**.
   - Battery-only operation works.
   - Current draw is in the expected range.
4. **USB data:** hold BOOTSEL (on the Pico) and plug in. RP2350 should appear as a USB drive through USB-C, which proves TP2/TP3 are soldered.
5. **SWD:** with the Tag-Connect and a Debug Probe, `openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg` should halt the core.
6. **Peripherals,** using a throwaway test firmware:
   - Buttons: read GPIO 12–15.
   - Display: ST7789 init, colour bars, backlight PWM.
   - SD: power on, card detect, read/write a test file, power off and check SD_VDD = 0 V.
   - SE: power on, read the OPTIGA UID (`optiga_util_read_data` on object 0xE0C2), power off.
   - Battery ADC reads ~VBAT/2.
   - Charge status reads correctly.
   - BLE: advertise and connect from a phone (nRF Connect).
7. **RF check:** BLE RSSI at 1 m and 5 m with the battery and display fitted.
8. **Do NOT burn any OTP fuses yet.** Secure boot, the debug disable, and the key hashes are Phase 2 steps, done only after the firmware signing flow is tested on a sacrificial board. OTP is irreversible, and getting it wrong bricks the board.

## 9. Simulation results (sim/)

`& "C:\Program Files\KiCad\10.0\bin\python.exe" hardware\sim\run_all.py` runs the whole suite. It takes about 15 s and writes [sim/report.md](sim/report.md). **Current status: 18/18 pass.**

### Bugs the simulation found (fixed, values only: footprints and routing unchanged)

| # | Problem found | Why it mattered | Fix |
|---|---|---|---|
| 1 | At reset the RP2350 pulls every GPIO **down** (36–113 k). That beat the 10 k / 100 k gate pull-ups, so the **SD card and backlight switched ON at boot**. The SE switch was also on with a low-threshold MOSFET. | "Default off" was false. The SE got powered before the firmware decided to power it. | Gate pull-ups R12/R21/R25 10 k/100 k → **2.2 k**; SD gate resistor R23 → **470 Ω** |
| 2 | Charger STAT drives 5.5 V × 20/30 = **3.66 V into GP5 while the RP2350 is unpowered** (switch OFF, charging). The datasheet limit for an unpowered FT pin is 3.63 V. | Slow damage or back-powering of the MCU while charging switched off | R8 20 k → **15 k** (max 3.30 V; still ≥ 2.85 V = logic high at 4.75 V VBUS) |
| 3 | The battery-sense divider (100 k/100 k) against the RP2350 ADC's **≥100 k input impedance** read up to **33 % low**. | The battery gauge would be useless | R10/R11 → **4.7 k** (worst case 2.3 %, calibratable; 0.45 mA, only while switched on) |
| 4 | After fix 1, the SD supply ramped in 0.09 ms, faster than the SD spec minimum of 0.1 ms. | Cards may not power-on-reset reliably | C8 100 nF → **4.7 µF** (ramp 0.72 ms, inrush 180 mA) |

I originally graded SE/SD switch-on against 50 mV / 300 mA targets I chose myself. The physically meaningful limit is that 3V3 stays ≥ 3.2 V, which it does (droop 62 mV / 10 mV), so those two are now reported as info.

### Rules the firmware must follow (Phase 2)

The simulation showed each of these failure modes when the rule is broken.

1. **Before powering the SD card off (GP7 high), drive GP0–GP3 low or set them as inputs.** Leaving CS high back-powers the card to 2.6 V through its I/O pins.
2. **After SD power-off, wait ≥ 150 ms before powering it back on.** SD_VDD needs about 132 ms to fall below 0.5 V, and the card only resets below that.
3. **Before powering the SE off (GP11 high), pull RST (GP10) low.** Otherwise the SE's internal RST pull-up feeds about 1.5 V into SE_VCC, a half-powered state (pull-up value assumed). SE_VCC takes about 130 ms to reach 0.3 V.
4. **GP28 (battery ADC):** use `adc_gpio_init` (pulls and digital input off), and store a **one-point gain calibration** per device to remove the ±2.3 % ADC-impedance error.
5. **GP5 (CHG_STAT):** read as an input with no internal pull. Low = charging, high = complete or no USB (use VBUS sense to tell those apart).
6. **Buttons:** the hardware RC already gives exactly one edge per press. Firmware still needs the hold-to-confirm logic.
7. **Before powering the display off (GP18 high), drive GP20/21/22/26/27 low** (or inputs). A pin left high back-powers the module to 2.5 V through its ESD diodes. After power-on, wait ≥ 2 ms (LCD_VCC reaches 90 % in 1.2 ms) before the ST7789 reset/init.
8. **Sleep:** with the display, SE and SD unpowered and the RP2350 dormant, the board draws **2.6 mA** (sim `off_drain`, MCU dormant assumed 1 mA: measure it). The rest is the VBAT divider (0.45 mA), the Pico's regulator and pull-ups. For storage, SW1 OFF draws 0.8 µA.

### What the simulation confirms

- Hot-plug ringing stays below absolute maximum: 5.6 V at the connector and 4.9 V on the board with PTC 0.15–0.3 Ω.
- 3V3 never dips below 3.2 V through plug, unplug, or a worst-case load on a 3.4 V battery.
- The switch-OFF state is truly off: 3V3 = 0 V and battery drain is 0.8 µA.
- Charging at 0.5 A CC / 4.20 V CV works with the switch on or off.
- CC sink detection works for all USB-C charger types and both plug orientations.
- I2C rise time is 120 ns, so Fast-mode passes with margin.
- The button debounce gives exactly one press and one release per actuation.
- Every bus lands on a legal RP2350 pin function.
