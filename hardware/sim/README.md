# Hardware simulation (Phase 1 board)

```powershell
& "C:\Program Files\KiCad\10.0\bin\python.exe" hardware\sim\run_all.py           # everything (~15 s)
& "C:\Program Files\KiCad\10.0\bin\python.exe" hardware\sim\run_all.py sd_power  # one test (name match)
& "C:\Program Files\KiCad\10.0\bin\python.exe" hardware\sim\breadboard.py        # breadboard prototype -> breadboard/TEST_REPORT.md
```

`breadboard.py` simulates the breadboard prototype from `tools/gen_breadboard.py`'s layout data (the same data the picture is drawn from), so a wire missing from the picture is missing from the simulation too.

The suite writes **[report.md](report.md)** (every check, its measured value and its limit) and SVG waveform plots in `out/`. The exit code is the number of failing tests, so it can gate a CI job or a pre-order checklist.

It uses the **ngspice engine that ships with KiCad**, driven through `ngspice.dll`, plus KiCad's bundled numpy. There's nothing to install.

## How it works

1. `netlist.py` reads **`tools/design.py`**, the same file that generates the schematic and PCB. It emits every resistor, capacitor, MOSFET, LED, switch, and connection as SPICE elements. If someone edits the design, the simulation follows automatically.
2. ICs and modules are **behavioural models** whose parameters come from their datasheets. The `MODEL NOTES` at the top of `netlist.py` list each one, its source, and which numbers are assumptions.
3. `scenarios.py` adds the outside world to the netlist:
   - a USB charger and cable
   - battery state
   - the power switch
   - what the firmware does with each GPIO
   - button presses with contact bounce
4. It then simulates and grades the results against datasheet or spec limits.
5. **Global absolute-maximum checks** run on every transient. They catch any test where a GPIO, the SE, the SD card, or the charger sees too much voltage, including the unpowered-pin case.
6. `static_checks.py` covers the checks that don't need SPICE:
   - RP2350 pin-mux legality
   - decoupling
   - power budget
   - KiCad ERC, DRC, and schematic↔PCB parity

## What's covered

| Test | Question it answers |
|---|---|
| pin_functions | Does each bus signal land on a GPIO that actually has that peripheral? |
| decoupling | Does every supply pin have a capacitor? |
| power_budget | Is the 3V3 load within the Pico's 300 mA? What's the battery life? |
| kicad_checks | Is ERC/DRC clean, fully routed, and the PCB matching the schematic? |
| reset_default_off | Do the SE, SD card, and backlight stay **off** while the RP2350 is in reset? Swept over pull-down 36–113 k and MOSFET threshold corners. |
| battery_boot | Does 3V3 come up cleanly from the battery? Any peripheral glitches? |
| usb_hotplug | Does hot-plug ringing stay below absolute maximum at both PTC resistance extremes? Does load-sharing keep 3V3 up through plug and unplug? |
| switch_off_usb | With the switch OFF: is 3V3 off, does charging continue, and does nothing overdrive the unpowered RP2350? |
| off_drain | What's the battery drain with the switch off? |
| charge_cycle | Does the charger do CC → CV → complete, with STAT and LED levels correct? |
| sd_power | Is the SD soft-start ramp inside the SD spec? What are the inrush and rail droop? Is the card back-powered after power-off? |
| se_power | Does the SE power-up behave? Does it reach a clean hibernate, or get back-fed through RST? |
| i2c_timing | Is the SDA rise time within I2C Fast-mode limits? |
| buttons | Does the RC debounce turn a bouncing contact into exactly one press and one release? |
| vbat_adc | How accurate is the battery reading, given the ADC's ≥100 k input? |
| usb_cc | Is the CC voltage in-spec for every charger type and plug orientation? |
| backlight_pwm | Does the backlight switch reproduce 1 kHz PWM cleanly? |
| worst_load | Does the board survive every load at once on a nearly empty battery? |

## What it does NOT cover (be honest with yourself)

- **RF / antenna, signal integrity of SPI/USB, EMI and ESD events:** these need a field solver or the real board.
- **The chips' internals:** no RP2350 firmware, no real SE050 protocol, no crypto. Those are tested in Phase 2 with host-based unit tests and on hardware.
- **Exact semiconductor behaviour:** the models are behavioural and level-1. They get levels, timing, and current paths right to within ~10 %, not the last millivolt.
- **Assumed values** are flagged in `netlist.py`:
  - Pico module capacitances
  - SE RST pull-up of 50 k
  - SD card internal capacitance
  - display module load
  - PTC resistance range
  - USBLC6 clamp voltage
- Battery capacity is scaled in `charge_cycle` so the cycle fits in seconds. Currents and voltages are real; times are not.
- **Bring-up on the real board (hardware/README.md §8) is still required.** The simulation finds design mistakes before you pay for boards; it does not replace measuring them.
