# tredder rev A in Fusion: simulation setup

Generated files (rebuild with `tools/export_mech.py` after any board change):

| File | Use it for |
|---|---|
| `tredder-revA.step` | Enclosure design (Phase 4) and fit checks. Every part, with the detailed Pico 2 W model. |
| `tredder-revA-sim.step` | **Simulation.** The board plus the 13 parts that matter mechanically or thermally. Each part is a few clean solids with no overlaps and no sub-0.3 mm bodies, so Fusion can mesh it. The Pico 2 W and the display module are simplified. |

Do not run a simulation on `tredder-revA.step`. The library Pico model has hundreds of tiny bodies (pins, pads, passives), and Fusion's mesher fails on them or takes hours.

The display, battery, USB-C, switches and JST are **envelopes**: boxes sized from the footprints and datasheets. They are not exact drawings. Check the display module against the one you buy (`tools/gen_step_models.py`, then re-export).

## 0. Quickest way: the Fusion script (makes the .f3d for you)

Only Fusion can write its own `.f3d` files, so this folder ships a script that you run once inside Fusion:

1. Unzip `tredder-fusion-TredderSim.zip`. You get a `TredderSim` folder; keep the files together.
2. In Fusion, go to **UTILITIES > ADD-INS > Scripts and Add-Ins**. Click **+**, choose **Script or add-in from device**, and pick the `TredderSim` folder.
3. Select **TredderSim** and click **Run**. The script:
   - imports the model into a new design;
   - names every body (for example "RP2350 - heat 0.10 W") and assigns the materials from section 2, creating the FR4, mold compound, LiPo and foam materials;
   - saves **`tredder-revA-sim.f3d`** in the same folder.
4. When it finishes, a message lists anything you need to fix by hand. Then go to **Simulation > New Simulation Study** and add the loads from sections 3 and 4.

Fusion's API cannot create simulation studies or loads, so those steps stay manual. The body names tell you which heat value goes where.

## 1. Import (by hand, without the script)

1. Open Fusion and go to **File > Open > Open from my computer**, then pick `tredder-revA-sim.step`. Wait for the upload and translation.
2. Switch the workspace from **Design** to **Simulation**, then choose **New Simulation Study**:
   - **Thermal** for heat.
   - **Static Stress** for button presses, the USB plug and screws.
   - **Modal Frequencies** or **Event Simulation** (drop) for vibration and shock.
3. Contacts: run **Automatic Contacts** with the default tolerance. The bodies touch face to face, so every contact comes out **Bonded**, which is correct for soldered and taped parts.

## 2. Bodies and materials

In **Materials > Study Materials**, use a Fusion library material where one exists, or **Manage Physical Materials > New** to make the custom ones.

| Body (component name) | Material | k (W/m·K) | Density (g/cm³) | Notes |
|---|---|---|---|---|
| `wallet_sim_PCB`: main board, 1.6 mm | custom "FR4 4-layer" | **20** | 1.9 | Two solid copper planes make heat spread much faster along the board than through it (about 0.4). Fusion only takes one value, so 20 is right for spreading heat across the board. |
| `pico2w_sim`: Pico PCB | custom "FR4 2-layer" | 10 | 1.9 | |
| `pico2w_sim`: RP2350, CYW43439 shield | mold compound (k 0.9) / **Steel** | 0.9 / 45 | 1.9 / 7.85 | Heat sources, see section 3 |
| `pico2w_sim`: micro-USB, BOOTSEL | Steel / ABS | | | |
| `lcd_..._sim`: header stack | **Nylon 6/6** (PBT in reality) | 0.25 | 1.3 | Holds the module 11 mm above the board |
| `lcd_..._sim`: module PCB | FR4 2-layer | 10 | 1.9 | |
| `lcd_..._sim`: LCD panel + backlight | **Glass** | 1.0 | 2.5 | Backlight heat source |
| `lipo_503035`: pouch | custom "LiPo cell" | 1.0 | 2.1 | Specific heat 1000 J/kg·K; mass about 10 g |
| `lipo_503035`: foam tape | custom "PE foam" | 0.05 | 0.3 | Insulates the cell from the board |
| `lipo_503035`: PCM | FR4 2-layer | 10 | 1.9 | |
| `usb_c_...`, `sw_slide_...` body, `sw_tact_...` body, `j3_sim` | **Steel** | 45 | 7.85 | Stamped-metal shells |
| switch actuators, plungers, `jst_...` | **Nylon 6/6** | 0.25 | 1.14 | |
| `u2_sim` (SE050), `u3_sim` (charger) | mold compound | 0.9 | 1.9 | |

## 3. Thermal loads (Thermal study)

Add each as an **Internal Heat** load on the body:

| Case | Body | Power | Where the number comes from |
|---|---|---|---|
| Awake, screen on | RP2350 | 0.10 W | about 30 mA at 3.3 V |
| | CYW43439 shield | 0.06 W | BLE connected, averaged |
| | LCD panel + backlight | 0.06 W | 18 mA backlight at 3.3 V |
| | Pico PCB | 0.03 W | buck converter losses |
| **Charging (worst case)** | `u3_sim` (MCP73831) | **0.65 W** | (5.0 V − 3.7 V) × 0.5 A. It reaches 1.0 W with a flat cell at 3.0 V, but the chip's thermal regulation throttles the current at a 110 °C junction. |
| | LiPo pouch | 0.04 W | I²R = 0.5² × 0.15 Ω |
| Sleeping | – | 0.01 W | 2.6 mA (circuit simulation); ignore it |

**Boundary condition:** apply **Convection** to every exposed face, at 5–10 W/m²·K (still air) and 25 °C ambient. Inside a closed case, add the case and use the case's outside faces instead.

**What to look for:**
- The LiPo pouch must stay **below 45 °C while charging**. That is the usual charging limit for LiPo cells. Check whether U3's heat, conducted through the board, reaches the cell (the foam tape helps).
- U3's case temperature should stay below about 100 °C.

## 4. Mechanical loads (Static Stress / Modal / Event)

**Constraints:** fix the inner cylindrical faces of the 4 M2 holes H1–H4 (**Fixed**, or **Pin** to let the screws rotate).

| Load | Body / face | Force |
|---|---|---|
| Button press (finger) | top face of a `sw_tact` plunger | 5 N (the switch needs 1.6 N; a thumb pushes harder) |
| Slide switch | side face of the `sw_slide` actuator | 3 N |
| USB-C plug insertion | inside face of the `usb_c` shell (toward the board) | 20 N |
| Cable yank, sideways | `usb_c` shell | 10 N |
| Display pressed | top face of the LCD panel | 10 N: checks the header stack, which carries the whole module |

- **Modal Frequencies:** watch the display module. It sits 11 mm up on a single 8-pin header row, so expect it to be the lowest mode. If it is below about 200 Hz, plan a standoff or foam pad under its far end in the case.
- **Event Simulation:** for a drop from 1.2 m, give the whole assembly an initial velocity of 4.85 m/s toward a fixed rigid plate.

## 5. What Fusion cannot simulate

Fusion simulates heat and force. It cannot run the Pico's firmware or drive the screen. That is covered elsewhere:

- `firmware/platform/emu`: the real firmware with an emulated ST7789 screen, buttons, battery and SE050.
- `hardware/sim`: ngspice circuit simulation of the power paths, switches, charger and sleep current.
