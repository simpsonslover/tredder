"""
Write the project's own 3D models as STEP solids into hardware/3d/.

These parts have no model in KiCad's library (or only had a VRML model, which
KiCad leaves out of STEP exports, so Fusion never saw the display or the
battery). The models are envelopes made of boxes: outline from the
footprint's Fab layer or datasheet, heights from datasheets. They are good for
fit checks and simulation. They are not detailed drawings.

Model frame (KiCad): origin = footprint origin, +x = footprint +x,
+y = footprint -y, +z = up from the board surface (away from the board for
bottom-side parts). Units mm.

  *_sim.step  simplified variants used by export_mech.py for the Fusion
              simulation file: no tiny bodies and no overlapping solids, so
              Fusion's mesher accepts them.

Run: python tools/gen_step_models.py   (plain python, no KiCad needed)
"""
import os

from step_writer import Step

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "3d")

GOLD, BLACK, SILVER = (0.85, 0.7, 0.2), (0.05, 0.05, 0.05), (0.8, 0.8, 0.82)
GREEN, BLUE, GLASS = (0.1, 0.35, 0.15), (0.1, 0.25, 0.6), (0.02, 0.02, 0.05)
NYLON, WHITE = (0.92, 0.9, 0.82), (0.95, 0.95, 0.95)

# ---- display module on J4 (2.0in 240x320 ST7789, ~60 x 36 mm; check against your module) ----
SOCKET_H = 8.5      # J4 PinSocket_1x08 body height
SPACER_H = 2.54     # male header plastic on the module
PCB_T = 1.6
PANEL_T = 2.4
X0, X1 = -2.5, 57.5          # module PCB, model frame (board x 2 .. 62)
Y0, Y1 = 4.0, -32.0          # (board y 2 .. 38)
Z_PCB = SOCKET_H + SPACER_H
PW, PH = 49.5, 34.0          # metal frame + backlight
PCX, CY = X1 - 2.0 - PW / 2, (Y0 + Y1) / 2
HDR_Y0, HDR_Y1 = 1.27, -2.54 * 7 - 1.27   # 8 pins at y = 0, -2.54 ... -17.78


def lcd(sim):
    s = Step("lcd_2in0_st7789_module" + ("_sim" if sim else ""))
    if sim:   # J4 socket + header plastic as one block (replaces the library socket model)
        s.box(-1.27, HDR_Y0, 0, 1.27, HDR_Y1, Z_PCB, BLACK, "header stack (PBT)")
    else:     # the library socket model is kept on J4; add the module's male header
        s.box(-1.25, HDR_Y0, SOCKET_H, 1.25, HDR_Y1, Z_PCB, BLACK, "male header")
        for i in range(8):
            y = -2.54 * i
            s.box(-0.32, y - 0.32, SOCKET_H - 3.0, 0.32, y + 0.32, SOCKET_H, GOLD, f"pin {i + 1}")
    s.box(X0, Y1, Z_PCB, X1, Y0, Z_PCB + PCB_T, BLUE, "module PCB (FR4)")
    z = Z_PCB + PCB_T
    s.box(PCX - PW / 2, CY - PH / 2, z, PCX + PW / 2, CY + PH / 2, z + PANEL_T, SILVER, "LCD panel + backlight")
    if not sim:  # active area 40.8 x 30.6 mm (320x240 landscape)
        s.box(PCX - 2.0 - 20.4, CY - 15.3, z + PANEL_T, PCX - 2.0 + 20.4, CY + 15.3, z + PANEL_T + 0.1, GLASS, "active area")
    return s


# ---- BT1 LiPo 503035 (model frame: origin = cell centre, +z away from the board) ----
CELL_L, CELL_W, CELL_T, TAPE = 35.0, 30.0, 5.0, 0.5   # same as L, W, T, TAPE in add_battery.py


def lipo():
    s = Step("lipo_503035")
    pl = CELL_L - 4.0
    s.box(-(CELL_L - 6) / 2, -(CELL_W - 4) / 2, 0, (CELL_L - 6) / 2, (CELL_W - 4) / 2, TAPE, WHITE, "foam tape")
    s.box(-pl / 2, -CELL_W / 2, TAPE, pl / 2, CELL_W / 2, TAPE + CELL_T, (0.52, 0.54, 0.6), "LiPo pouch")
    s.box(-pl / 2 - 3.0, -(CELL_W - 4) / 2, TAPE + 1.0, -pl / 2, (CELL_W - 4) / 2, TAPE + 4.0, (0.85, 0.65, 0.2), "protection PCM")
    return s


# ---- parts KiCad 10's library has no model for (outlines from their footprints' F.Fab) ----
def usb_c():   # HRO TYPE-C-31-M-12, top mount, 8.94 x 7.3 x 3.16
    s = Step("usb_c_hro_type_c_31_m_12")
    s.box(-4.47, -3.65, 0, 4.47, 3.65, 3.16, SILVER, "USB-C shell")
    return s


def slide():   # C&K JS202011JCQN, body 9.0 x 3.6, actuator on top
    s = Step("sw_slide_ck_js202011jcqn")
    s.box(-4.5, -1.8, 0, 4.5, 1.8, 2.5, SILVER, "body")
    s.box(-1.65, -0.75, 2.5, -0.15, 0.75, 4.0, BLACK, "actuator")
    return s


def tact():    # XKB TS-1187A, 5.1 x 5.1 x 1.5
    s = Step("sw_tact_xkb_ts1187a")
    s.box(-2.55, -2.55, 0, 2.55, 2.55, 1.1, SILVER, "body")
    s.box(-0.95, -0.95, 1.1, 0.95, 0.95, 1.5, BLACK, "plunger")
    return s


def jst():     # JST S2B-PH-SM4-TB side entry; height 6.0 is a safe envelope (check the datasheet)
    s = Step("jst_ph_s2b_ph_sm4_tb")
    s.box(-3.95, -4.45, 0, 3.95, 3.25, 6.0, NYLON, "housing")
    return s


def hx2qfn20():   # NXP SE050, HX2QFN20 / SOT1969-1: 3 x 3 x 0.32 mm (KiCad ships no model for it)
    s = Step("nxp_hx2qfn20_se050")
    s.box(-1.5, -1.5, 0, 1.5, 1.5, 0.32, BLACK, "body")
    return s


def pico_sim():
    """Pico 2 W as a few solids for simulation (the library model has hundreds of tiny bodies).
    USB end at footprint -y (model +y), antenna end at model -y. Chip positions are approximate."""
    s = Step("pico2w_sim")
    s.box(-10.5, -25.5, 0, 10.5, 25.5, 1.0, GREEN, "Pico PCB (FR4)")
    s.box(-4.0, 21.8, 1.0, 4.0, 26.8, 3.7, SILVER, "micro-USB")
    s.box(-3.5, -1.5, 1.0, 3.5, 5.5, 1.85, BLACK, "RP2350 (heat source)")
    s.box(-5.0, -18.5, 1.0, 5.0, -8.5, 2.3, SILVER, "CYW43439 shield (heat source)")
    s.box(-7.8, 12.0, 1.0, -4.2, 15.5, 2.5, WHITE, "BOOTSEL")
    return s


ALL = [lambda: lcd(False), lambda: lcd(True), lipo, usb_c, slide, tact, jst, hx2qfn20, pico_sim]


def main():
    os.makedirs(OUT, exist_ok=True)
    for make in ALL:
        s = make()
        s.write(os.path.join(OUT, s.name + ".step"))
        print("wrote 3d/" + s.name + ".step", len(s.solids), "solids")
    for old in ("lcd_2in0_st7789_module.wrl", "lipo_503035.wrl"):  # replaced by the STEP files
        p = os.path.join(OUT, old)
        if os.path.exists(p):
            os.remove(p)


if __name__ == "__main__":
    main()
