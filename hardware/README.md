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
| `wallet.kicad_dru` + `wallet.kicad_pro` constraints | **JLCPCB's published multilayer limits** (jlcpcb.com/capabilities/pcb-capabilities, read 2026-11-01; re-check before ordering): track/space 0.09, via 0.15/0.25, PTH ring ≥ 0.15, PTH hole to track ≥ 0.28, SMD pad spacing ≥ 0.15, pad to track ≥ 0.1, copper to edge ≥ 0.2, NPTH ≥ 0.5, inner hole clearances 0.2/0.3, silkscreen text ≥ 1.0 mm / line ≥ 0.15 mm. KiCad's DRC uses them automatically |
| `jlcpcb-4L-real.edru` | The same JLCPCB limits as a **Fusion** design-rules file (load it in Fusion's DRC). It cannot hold the PTH-to-PTH 0.45 mm, PTH-hole-to-track 0.28 mm, NPTH and silkscreen limits; those are checked on the KiCad side. Also inside `mech/tredder-revA-fusion-ecad.zip` |
| `tools/gerber_drc.py` → `fab/gerber_drc.md` | **Independent DRC/DFM of the files the fab receives**, like EAGLE/Fusion's DRC and JLCPCB's DFM: renders the Gerbers at 10 µm (pygerber), reads the drill files and the IPC-D-356 netlist, and checks connectivity (opens = Fusion's air wires, shorts, inner planes included), clearance, width, annular rings, hole spacing, hole-to-copper, board edge, mask webs, silkscreen. Uses none of KiCad's checks. Proven on injected faults (missing plane, short, hairline). `export_fab.py` refuses to build the order if it reports an error. Needs `pip install pygerber numpy scipy pillow` |
| `tools/check_jlc.py` | JLCPCB hole-to-hole spacing (PTH ≥ 0.45 mm, other ≥ 0.2 mm), which KiCad 10's DRC does not check in this project; `export_fab.py` refuses to build the order without it |
| `tools/clean_tracks.py` | Removes Freerouting's duplicate segments and dead-end spurs (Fusion's "Wire Stub" check) without changing a connection; `route.py` runs it after every import |
| `tools/sync_values.py` | Pushes value-only changes from `design.py` into the routed PCB without touching copper |
| `breadboard/` | **Breadboard prototype** with the same Pico pins as the PCB: wiring picture, parts, wire-by-wire table, what it can and can't test (`tools/gen_breadboard.py`) |
| `mech/` | STEP for Fusion / MCAD: `tredder-revA.step` (detailed, for the enclosure) and `tredder-revA-sim.step` (clean solids for simulation). Setup: [mech/FUSION_SIM.md](mech/FUSION_SIM.md). Rebuild: `tools/export_mech.py` (KiCad python) |
| `mech/tredder-revA-fusion-ecad.zip` | The KiCad project for **Fusion's electronics import** (schematic + board). KiCad 10 writes nets in a form Fusion can't read (every pad comes in with no net), so `tools/export_fusion_ecad.py` writes the board with KiCad 9 net syntax and checks every net survives. Import `wallet.kicad_pro` from this zip, not from this folder |
| `3d/` | The project's own STEP models (display module, LiPo, and parts missing from KiCad's library), from `tools/gen_step_models.py` |

---

## 1. Block diagram

```
 USB-C ──F1──U1(ESD)──R3/R4(0Ω, DNP=charge-only)──► Pico TP2/TP3 (USB D-/D+)
   │ VBUS
   ├────────► Pico VBUS ──(Schottky on module)──► VSYS ◄──Q1 (load-share PFET)◄── SW1-A ◄── BT1 LiPo (J2)
   └──► U3 MCP73831 charger ───────────────────────────────────────────────────────┘ VBAT
                                                    SW1-B: 3V3_EN → GND (hard off, even on USB)

 Pico 2 W (RP2350 + CYW43439 BLE)
   I2C0  GP8/GP9  ──────► U2 NXP SE050C2         (VCC switched by Q2 = its reset; pull-ups on the SE supply)
   SPI0  GP0-3    ──────► J3 microSD            (VDD switched by Q3, soft-start, DET on GP6)
   SPI1  GP26/27  ──────► J4 2.0" ST7789 module (CS GP21, DC GP22, RST GP20, BL via Q4 on GP19,
                          module VCC switched by Q5 on GP18: unpowered in sleep)
   GP12-15        ──────► Back / Confirm / Up / Down (10k pull-ups + RC debounce)
   GP28 (ADC2)    ◄────── VBAT divider (after power switch, no drain when off)
   GP5            ◄────── charger STAT (5V→3.3V divider)
   SWD pads D1-D3 ──────► reachable from the back through the plated holes under the Pico (no debug connector)
```

## 2. Pin map

| GPIO | Function | | GPIO | Function |
|---|---|---|---|---|
| GP0 | SD MISO (SPI0 RX) | | GP14 | BTN UP |
| GP1 | SD CS (SPI0 CSn) | | GP15 | BTN DOWN |
| GP2 | SD SCK | | GP16 | unused |
| GP3 | SD MOSI | | GP17 | unused |
| GP4 | unused | | GP18 | LCD power enable (active low, Q5) |
| GP5 | CHG_STAT | | GP19 | LCD backlight (active low) |
| GP6 | SD card detect | | GP20 | LCD RST |
| GP7 | SD power enable (active low) | | GP21 | LCD CS |
| GP8 | SE SDA (I2C0) | | GP22 | LCD DC |
| GP9 | SE SCL (I2C0) | | GP26 | LCD SCK (SPI1) |
| GP10 | unused | | GP27 | LCD MOSI (SPI1) |
| GP11 | SE power enable (active low) | | GP28 | VBAT sense (ADC2) |
| GP12 | BTN BACK | | GP23-25, 29 | used inside the module by the radio |
| GP13 | BTN CONFIRM | | | |

All five switch controls (Q2 SE, Q3 SD, Q4 backlight, Q5 display power, and SW1) default to **off** when the MCU is in reset or unprogrammed.

## 3. Secure element: NXP SE050C2 (chosen over the SE050A2)

**U2 = NXP SE050C2, `SE050C2HQ1/Z01SDZ`** (HX2QFN20 / SOT1969-1: 3 × 3 × 0.32 mm, 0.4 mm pitch, centre pad to GND). **You buy it from an NXP-authorised distributor (Mouser / Digi-Key / Farnell) and solder it yourself.** It's left out of the JLCPCB BOM and placement file. LCSC lists it as C2659345, but with almost no stock.

**Why C2 rather than A2.** Both are the same chip, pinout and package, with the same certification (CC EAL6+), bus encryption (SCP03) and 50 kB of secure storage. The suffix is the configuration NXP loads at the factory:

| | **SE050C2** | SE050A2 |
|---|---|---|
| Public-key crypto | Full set: ECC (NIST, Brainpool, Ed25519/X25519, **Koblitz incl. secp256k1**) and RSA up to 4096 bit | A subset: ECC only, no RSA (NXP: the A/B variants "come with a subset of keys") |
| secp256k1, the Bitcoin/Ethereum curve | Users report ECDSA secp256k1 signing works on the C2 dev kit | Not confirmed for A |
| Dev kit for the breadboard | OM-SE050ARD is the C2 configuration | — |

The C2 gives up nothing against the A2, and keeps the option of doing secp256k1 operations inside the SE (for example a device attestation key the app can verify).

**What changed from the OPTIGA Trust M, and what that trades off:**

- **Bus protection.** The OPTIGA's Shielded Connection is replaced by the SE050's **Platform SCP03** (AES-encrypted and MACed APDUs). **Trade-off:** NXP publishes the default SCP03 keys for its dev kits, so the link is effectively plaintext until provisioning rotates them to device-unique keys stored in RP2350 OTP. Firmware must refuse to store any secret before that rotation.
- **PIN-gated secret.** On the SE050 this is an AES authentication object with MaxAttempts = 10 guarding a binary object (firmware `app/se.h`). **Trade-off:** Trezor's open-source PIN code was written for the OPTIGA. For the SE050 the references are NXP's AN12413 (APDU spec) and AN12514 (security guidelines), and Nitrokey 3, an open-source security key built on the SE050. The "attempt counted before the result" property must be re-verified on this chip in M2.
- **Soldering.** **Trade-off:** the pitch is 0.4 mm instead of 0.5 mm, which is harder by hand. Use flux, a fine tip or solder paste, and hot air or a hot plate, then check every side under a magnifier for bridges.
- **No reset pin.** The SE050 is reset by switching its supply off with Q2 for ≥ 40 ms (GP10 is now unused). ENA is tied high (deep power-down not used), VIN = VCC, VOUT open, and the unused contactless (LA/LB) and ISO7816 RST inputs go to GND, all per datasheet Table 5.
- **I2C pull-ups** R13/R14 now go to the switched SE supply, not +3V3, so an unpowered SE is never fed through its I/O pins.
- **Lifecycle.** Distributors list the SE050C2 as NRND (not recommended for new designs), with NXP steering new designs to newer SE05x parts. It is still stocked; **buy 2–3 spares now.**

**Phase 2 key scheme (design target):**
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
| No test points on SE I2C | Makes passive probing slightly harder (Platform SCP03 is the real defence) | Harder to debug the SE link (use a scope on the IC pins) |
| No debug connector or test pads (removed to keep the board simple) | Less to cover in production; SWD disabled in OTP anyway | Debugging means soldering wires to the Pico's underside SWD pads through the holes; voltages are probed on the parts themselves |
| External pull-ups on buttons | Immune to RP2350 erratum E9 (input pull-down latch) whatever the chip stepping | 8 passives |
| All parts on top, display module above them | Single-sided assembly is cheaper | Display must be removed to reach the Pico (BOOTSEL, SWD) |

## 5. Honest physical-security assessment

**What this design resists**
- **Lost or stolen device, casual or opportunistic attacker:** strong. Without the PIN, the seed is ciphertext in external flash. The SE wipes its secret after 10 tries, and that counter lives in the SE, not in the MCU.
- **Reading the flash chip** (desoldering the W25Q on the Pico module): gets ciphertext only. This is exactly why the seed must never be in flash in plain form.
- **Firmware replacement:** secure boot rejects unsigned images, and anti-rollback rejects old ones.
- **SWD debugging:** disabled permanently in OTP.
- **Sniffing the MCU↔SE bus:** defeated by Platform SCP03, once provisioning has replaced NXP's published default keys.

**What it does not fully resist (be aware)**
- **Fault injection on the RP2350 (voltage/EM/laser glitching).** In Raspberry Pi's 2024–25 RP2350 hacking challenge, researchers broke the A2 stepping with glitching, and one team extracted OTP secrets. The A4 stepping fixed several of these, and the glitch detectors help, but a well-equipped lab attacker with your device for days is **not** stopped by the RP2350 alone. That's why the SE secret is part of the key derivation: glitching the MCU alone then shouldn't be enough.
- **The SE itself:** EAL6+ resists most lab attacks within its certified scope, but no chip is unbreakable. A nation-state lab attack is out of scope.
- **Evil maid / supply-chain implant:** an attacker who opens the case, adds a keylogger on the buttons or a tap on the display SPI, and returns the device could capture the PIN or the seed shown during setup. Mitigations: the tamper-evident case (Phase 4), checking the seal every time, and a firmware attestation check against the SE (Phase 2). The board itself can't detect this when it's off; there's no battery-backed tamper mesh.
- **Wireless:** the CYW43439 runs a closed-source Infineon firmware blob loaded by the RP2350. A remote bug there is a real (if unlikely) risk. The BLE design treats the phone and the radio as untrusted (everything authenticated, confirm on device), but an air-gapped design such as SeedSigner or Coldcard with QR/SD is strictly safer. **This is the single biggest trade-off of putting Bluetooth in the device.**
- **Cold-boot / RAM remanence while unlocked:** the firmware wipes secrets after use, but an attacker who glitches a running, unlocked device has more options than one who gets a locked device.
- **$5 wrench attack:** use the BIP39 passphrase (hidden wallet) feature in Phase 2.

## 6. Check these before ordering ⚠

1. **SE050 footprint:** `NXP_HX2QFN-20-1EP_3x3mm_P0.4mm_EP1.7x1.7mm` is KiCad's WQFN-20 3×3 P0.4 land pattern (0.2 × 0.825 mm pads, 1.7 × 1.7 mm centre pad). It matches the EasyEDA footprint LCSC publishes for C2659345, and the pinout is datasheet rev 3.7 Figure 4 / Table 5. NXP's site blocks automated downloads, so I couldn't read the SOT1969-1 package drawing itself. **Compare the pads with NXP's SOT1969-1 "reflow soldering footprint" before ordering.**
2. **Display module pinout and outline:** J4 assumes the common 2.0" 240×320 ST7789V module order **GND, VCC, SCL, SDA, RES, DC, CS, BLK** and a ~60 × 36 mm outline (F.Fab rectangle). Waveshare and others use different orders. Buy the module first, then edit `NETS` in `design.py` to match. The firmware drives the panel landscape (`MADCTL 0x60` in `firmware/app/src/gfx.c`), assuming the module's native top edge points away from J4. If the picture is upside down on your module, use `0xA0`.
3. **LiPo connector polarity:** pin 1 = BAT+. Many cheap packs are wired reversed for JST-PH. A reversed pack destroys the charger. Use a cell **with a protection circuit (PCM)**, ≤ 50 × 34 × 6 mm to sit behind the board, ≥ 500 mAh (charge current is set to 500 mA by R5 = 2 kΩ). The design uses **BT1 = 1S LiPo "503035" (5.0 × 30 × 35 mm, 500 mAh, PCM, JST-PH 2.0 lead)**: not a board part: it is drawn as an outline on B.Fab, and its STEP model rides on J2, the connector it plugs into (`tools/add_battery.py`). This keeps every board part matched to a schematic symbol, which Fusion's import requires, stuck behind the left half with 0.5 mm foam tape, clear of the J4 header pins and the antenna. It is not in the JLC BOM: buy it separately and **check the lead polarity before plugging in**.
4. **LCSC numbers:** all checked against their LCSC product pages on 2026-11-02 (MPN, maker, value and package match for C1779, C19666, C22809, C4190, C23179, C424093). Stock was not visible there: JLCPCB's BOM step shows it for every line. Check it before paying.
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
   - SW1 ON → 3.3 V on the Pico's 3V3_OUT pin (pin 36) or across C5/C9.
   - SW1 OFF → 0 V, **even with USB connected**.
   - Battery-only operation works.
   - Current draw is in the expected range.
4. **USB data:** hold BOOTSEL (on the Pico) and plug in. RP2350 should appear as a USB drive through USB-C, which proves TP2/TP3 are soldered.
5. **SWD (optional):** solder three thin wires to the Pico's underside pads D1 (SWCLK), D2 (GND), D3 (SWDIO) through the holes from the back, connect a Debug Probe, and `openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg` should halt the core.
6. **Peripherals,** using a throwaway test firmware:
   - Buttons: read GPIO 12–15.
   - Display: ST7789 init, colour bars, backlight PWM.
   - SD: power on, card detect, read/write a test file, power off and check SD_VDD = 0 V.
   - SE: power on, read the SE050's unique ID with NXP's Plug & Trust middleware (I2C address 0x48), power off.
   - Battery ADC reads ~VBAT/2.
   - Charge status reads correctly.
   - BLE: advertise and connect from a phone (nRF Connect).
7. **RF check:** BLE RSSI at 1 m and 5 m with the battery and display fitted.
8. **Do NOT burn any OTP fuses yet.** Secure boot, the debug disable, and the key hashes are Phase 2 steps, done only after the firmware signing flow is tested on a sacrificial board. OTP is irreversible, and getting it wrong bricks the board.

## 9. Simulation results (sim/)

`& "C:\Program Files\KiCad\10.0\bin\python.exe" hardware\sim\run_all.py` runs the whole suite. It takes about 15 s and writes [sim/report.md](sim/report.md). **Current status: 19/19 pass.**

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
3. **SE050 reset = power cycle:** GP11 high for **≥ 40 ms** (SE_VCC takes about 35 ms to fall below 0.3 V). Keep GP8/GP9 open-drain with no internal pull-ups: driving SDA high while the SE is off back-powers it to about 1.5 V through the pull-up and its weak internal pull-up.
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
