# Breadboard simulation report

Generated 2026-11-07 18:06 by `sim/breadboard.py` from the layout in `tools/gen_breadboard.py` (ngspice from KiCad + behavioural models; see `sim/README.md` for what is and isn't modelled).

| Status | Test | What it checks |
|---|---|---|
| **PASS** | [bb_layout](#bb-layout) | Breadboard picture: holes, netlist, Pico pins, buttons |
| **PASS** | [bb_battery](#bb-battery) | Running from the battery (no USB) |
| **PASS** | [bb_usb](#bb-usb) | USB plugged in: charging and power path |
| **PASS** | [bb_load](#bb-load) | Worst-case load on a nearly empty battery |
| **PASS** | [bb_i2c](#bb-i2c) | SE050 kit I2C over jumper wires |
| **PASS** | [bb_inputs](#bb-inputs) | Buttons read correctly on internal pull-ups, GP6 held high |

## bb_layout

**Breadboard picture: holes, netlist, Pico pins, buttons** - PASS (0.0 s)

Static check of the layout data the picture is drawn from (gen_breadboard.check_layout): every hole exists and holds one leg/wire, strips + wires make exactly the intended netlist, each Pico pin has the PCB's net, buttons work either way round. Then the same check on the picture with the battery - lead removed must fail (proves the check sees it).

| Result | Check | Value | Limit |
|---|---|---|---|
| PASS | problems found in the layout | 0  | 0 |
| INFO | legs / wires checked | 62 / 39  |  |
| PASS | self-test: battery - lead removed is reported | 3 problems | >= 1 |

## bb_battery

**Running from the battery (no USB)** - PASS (0.4 s)

Battery at 4.2 V (full), 3.7 V and 3.3 V (nearly empty); MCU 60 mA, display + backlight, SE, SD idle. VSYS = battery - diode drop; the Pico's buck-boost must hold 3.3 V; GP28 (ADC, pulls off, ADC input >= 100 k) must read half the battery voltage. Last row: the same circuit with the battery - lead missing (the first version of the picture).

| Result | Check | Value | Limit |
|---|---|---|---|
| PASS | [4.2 V] VSYS (RT6154 needs >= 1.8 V) | 3.88 V | >= 1.8 V |
| PASS | [4.2 V] 3V3 rail | 3.295 V | 3.2 .. 3.4 V |
| PASS | [4.2 V] GP28 / (VBAT/2) (divider accuracy) | 0.977  | 0.97 .. 1.01  |
| INFO | [4.2 V] diode drop VBAT - VSYS | 0.3067 V |  |
| INFO | [4.2 V] battery current | 114.9 mA |  |
| PASS | [3.7 V] VSYS (RT6154 needs >= 1.8 V) | 3.372 V | >= 1.8 V |
| PASS | [3.7 V] 3V3 rail | 3.295 V | 3.2 .. 3.4 V |
| PASS | [3.7 V] GP28 / (VBAT/2) (divider accuracy) | 0.977  | 0.97 .. 1.01  |
| INFO | [3.7 V] diode drop VBAT - VSYS | 0.3119 V |  |
| INFO | [3.7 V] battery current | 132.1 mA |  |
| PASS | [3.3 V] VSYS (RT6154 needs >= 1.8 V) | 2.965 V | >= 1.8 V |
| PASS | [3.3 V] 3V3 rail | 3.295 V | 3.2 .. 3.4 V |
| PASS | [3.3 V] GP28 / (VBAT/2) (divider accuracy) | 0.977  | 0.97 .. 1.01  |
| INFO | [3.3 V] diode drop VBAT - VSYS | 0.3168 V |  |
| INFO | [3.3 V] battery current | 150.1 mA |  |
| RULE | FIRMWARE: GP28 left with its reset pull-down (36 k worst case): GP28 / (VBAT/2) | 0.9387  |  |
| RULE | battery - not wired (old picture): 3V3 rail | 3.787e-09 V |  |

## bb_usb

**USB plugged in: charging and power path** - PASS (0.1 s)

5 V on the Pico's micro-USB, battery at 3.7 V. The charger runs from VBUS (pin 40) at its 100 mA setting; the Pico runs from VBUS through its own diode, so D1 must be reverse-biased (the battery isn't discharged while charging).

| Result | Check | Value | Limit |
|---|---|---|---|
| PASS | charge current into the battery | 99.57 mA | 85 .. 115 mA |
| PASS | VBUS (MCP73831 VDD max 6 V; Pico VSYS max 5.5 V) | 4.96 V | <= 5.5 V |
| PASS | VSYS above the battery (D1 reverse-biased) | 0.9481 V | >= 0.2 V |
| PASS | 3V3 rail | 3.295 V | 3.2 .. 3.4 V |
| INFO | VSYS | 4.66 V |  |
| PASS | full battery: charger stops pushing current | -0.2151 mA | <= 10 mA |

## bb_load

**Worst-case load on a nearly empty battery** - PASS (0.1 s)

Battery 3.3 V, no USB. MCU 80 mA + radio bursts 60 mA (1 ms every 4 ms), SD card writing 100 mA, backlight on, SE active (19 mA), all from the Pico's 3V3 pin. Pico datasheet: keep 3V3 output <= 300 mA.

| Result | Check | Value | Limit |
|---|---|---|---|
| PASS | 3V3 minimum | 3.288 V | >= 3.15 V |
| PASS | VSYS minimum | 2.892 V | >= 1.8 V |
| PASS | peak 3V3 output current | 298.3 mA | <= 300 mA |
| INFO | peak battery current | 386.5 mA |  |

## bb_i2c

**SE050 kit I2C over jumper wires** - PASS (0.4 s)

GP8 open-drain toggling at 400 kHz (Fast mode), kit pull-ups 3.3 k, SE050 pins, 30 pF of wire per line (assumed). I2C Fast mode: rise time (30 -> 70 %) <= 300 ns, VOL <= 0.4 V.

| Result | Check | Value | Limit |
|---|---|---|---|
| PASS | SDA rise time 30 -> 70 % | 107.5 ns | <= 300 ns |
| PASS | SDA low level | 0.0466 V | <= 0.4 V |
| PASS | SDA idle high | 3.295 V | >= 2.30667 V |

## bb_inputs

**Buttons read correctly on internal pull-ups, GP6 held high** - PASS (0.0 s)

Pico pins as inputs (pulls off). CONFIRM pressed at 5 ms; the other buttons idle. The SD module has no card-detect pin, so GP6 (SD_DET) must sit at a defined high level. No resistors on the breadboard: GP6 and GP12-GP15 use the RP2350's internal pull-ups (modelled as 32 k, the weak end).

| Result | Check | Value | Limit |
|---|---|---|---|
| PASS | BTN_BACK released (VIH 2.0 V) | 3.295 V | >= 2 V |
| PASS | BTN_CONFIRM released (VIH 2.0 V) | 3.295 V | >= 2 V |
| PASS | BTN_UP released (VIH 2.0 V) | 3.295 V | >= 2 V |
| PASS | BTN_DOWN released (VIH 2.0 V) | 3.295 V | >= 2 V |
| PASS | BTN_CONFIRM pressed (VIL 0.8 V) | 1.03e-05 V | <= 0.8 V |
| PASS | SD_DET (GP6) held high by the internal pull-up | 3.295 V | >= 2 V |
| RULE | FIRMWARE: the breadboard needs the internal pull-ups on GP6 and GP12-GP15 (no external resistors) |   |  |
| RULE | no card detect on the breadboard: firmware finds the card by initialising it |   |  |
