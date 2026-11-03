"""
Export the board as STEP for Fusion (or any MCAD tool) into hardware/mech/.

  tredder-revA.step       everything, detailed: enclosure design (Phase 4), fit checks
  tredder-revA-sim.step   simulation-ready: board + the parts that matter mechanically
                          or thermally, each a few clean solids that do not overlap
                          (the Pico 2 W and the display get simplified models)

Run with KiCad's python:  & "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tools/export_mech.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
import gen_step_models  # noqa: E402
from step_writer import Step  # noqa: E402

CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
PCB = os.path.join(HW, "wallet.kicad_pcb")
OUT = os.path.join(HW, "mech")
M3D = os.path.join(HW, "3d").replace("\\", "/") + "/"

SIM_MODELS = {"A1": "pico2w_sim.step", "J4": "lcd_2in0_st7789_module_sim.step"}
# parts with detailed library models: replaced by one box (height mm, colour, footprint rect or None = Fab outline)
SIM_BOXES = {"J3": (2.0, (0.75, 0.75, 0.78), (-6.92, -7.83, 6.92, 8.12)),  # Hirose DM3AT body (Fab also draws the card)
             "U2": (0.32, (0.1, 0.1, 0.1), None),     # SE050, HX2QFN-20
             "U3": (1.1, (0.1, 0.1, 0.1), None)}      # MCP73831 charger, SOT-23-5 (heat source while charging)
SIM_REFS = ["A1", "J1", "J2", "J3", "J4", "SW1", "SW2", "SW3", "SW4", "SW5", "U2", "U3"]   # J2 also carries the battery model


def step(board_file, out, filt=None):
    cmd = [CLI, "pcb", "export", "step", "--no-dnp", "--force", "-o", out]
    if filt:
        cmd += ["--component-filter", ",".join(filt)]
    r = subprocess.run(cmd + [board_file], capture_output=True, text=True)
    log = r.stdout + r.stderr
    bad = [l for l in log.splitlines() if "Could not add" in l or "VRML" in l or "File not found" in l]
    if r.returncode != 0 or bad or not os.path.exists(out):
        sys.exit("STEP export failed for " + out + ":\n" + log)
    print("wrote", os.path.relpath(out, HW))


def fab_bbox(fp):
    """Fab-layer outline of fp in its own frame (orientation 0), mm."""
    f = pcbnew.FOOTPRINT(fp)
    f.SetOrientationDegrees(0)
    f.SetPosition(pcbnew.VECTOR2I(0, 0))
    layer = pcbnew.B_Fab if f.IsFlipped() else pcbnew.F_Fab
    xs, ys = [], []
    for g in f.GraphicalItems():
        if g.GetLayer() == layer and g.GetClass() == "PCB_SHAPE":
            bb = g.GetBoundingBox()
            xs += [bb.GetLeft(), bb.GetRight()]
            ys += [bb.GetTop(), bb.GetBottom()]
    if not xs:
        sys.exit(f"{fp.GetReference()}: no Fab outline to make a simulation box from")
    return [pcbnew.ToMM(v) for v in (min(xs), min(ys), max(xs), max(ys))]


def sim_board(tmp):
    """Board copy with the simulation models. Returns (board file, Step objects of the per-part boxes)."""
    b = pcbnew.LoadBoard(PCB)
    boxes = []
    for fp in b.GetFootprints():
        ref = fp.GetReference()
        # the board is saved elsewhere, so ${KIPRJMOD} would point at the wrong folder
        files = [m.m_Filename.replace("${KIPRJMOD}/3d/", M3D) for m in fp.Models()]
        if ref in SIM_MODELS:
            files = [M3D + SIM_MODELS[ref]]
        elif ref in SIM_BOXES:
            h, rgb, rect = SIM_BOXES[ref]
            x0, y0, x1, y1 = rect or fab_bbox(fp)
            s = Step(f"{ref.lower()}_sim")
            s.box(x0, -y1, 0, x1, -y0, h, rgb, fp.GetValue())   # model +y = footprint -y
            path = os.path.join(tmp, s.name + ".step").replace("\\", "/")
            s.write(path)
            boxes.append((ref, s))
            files = [path]
        fp.Models().clear()
        for f in files:
            m = pcbnew.FP_3DMODEL()
            m.m_Filename = f
            fp.Models().append(m)
    out = os.path.join(tmp, "wallet_sim.kicad_pcb")
    b.Save(out)
    return out, boxes


# ---- Fusion script package: materials and the body each sim solid becomes ----
MATERIALS = {   # props: (value, units); no props = Fusion library material as it is
    "fr4_4": {"name": "FR4 4-layer (tredder)", "base": ["FR-4", "FR4", "Epoxy", "ABS Plastic"],
              "props": {"Density": (1900, "kg/m^3"), "Thermal Conductivity": (20, "W/(m*K)"), "Specific Heat": (1100, "J/(kg*K)")}},
    "fr4_2": {"name": "FR4 2-layer (tredder)", "base": ["FR-4", "FR4", "Epoxy", "ABS Plastic"],
              "props": {"Density": (1900, "kg/m^3"), "Thermal Conductivity": (10, "W/(m*K)"), "Specific Heat": (1100, "J/(kg*K)")}},
    "mold": {"name": "Mold compound (tredder)", "base": ["Epoxy", "ABS Plastic"],
             "props": {"Density": (1900, "kg/m^3"), "Thermal Conductivity": (0.9, "W/(m*K)"), "Specific Heat": (900, "J/(kg*K)")}},
    "lipo": {"name": "LiPo cell (tredder)", "base": ["Aluminum", "ABS Plastic"],
             "props": {"Density": (2100, "kg/m^3"), "Thermal Conductivity": (1.0, "W/(m*K)"), "Specific Heat": (1000, "J/(kg*K)")}},
    "foam": {"name": "PE foam (tredder)", "base": ["Polyethylene", "ABS Plastic"],
             "props": {"Density": (300, "kg/m^3"), "Thermal Conductivity": (0.05, "W/(m*K)"), "Specific Heat": (1800, "J/(kg*K)")}},
    "steel": {"name": "Steel", "base": ["Steel", "Steel, Mild", "Stainless Steel"]},
    "glass": {"name": "Glass", "base": ["Glass"]},
    "nylon": {"name": "Nylon 6/6", "base": ["Nylon 6/6", "Nylon", "ABS Plastic"]},
    "abs": {"name": "ABS Plastic", "base": ["ABS Plastic"]},
}
# (model, box name) -> (material, label shown in Fusion)
BODIES = {
    ("pico2w_sim", "Pico PCB (FR4)"): ("fr4_2", "Pico 2 W PCB - heat 0.03 W (buck)"),
    ("pico2w_sim", "micro-USB"): ("steel", "Pico micro-USB"),
    ("pico2w_sim", "RP2350 (heat source)"): ("mold", "RP2350 - heat 0.10 W"),
    ("pico2w_sim", "CYW43439 shield (heat source)"): ("steel", "CYW43439 shield - heat 0.06 W"),
    ("pico2w_sim", "BOOTSEL"): ("abs", "Pico BOOTSEL"),
    ("lcd_2in0_st7789_module_sim", "header stack (PBT)"): ("nylon", "Display header stack (J4)"),
    ("lcd_2in0_st7789_module_sim", "module PCB (FR4)"): ("fr4_2", "Display module PCB"),
    ("lcd_2in0_st7789_module_sim", "LCD panel + backlight"): ("glass", "LCD panel - heat 0.06 W (backlight)"),
    ("lipo_503035", "foam tape"): ("foam", "Battery foam tape"),
    ("lipo_503035", "LiPo pouch"): ("lipo", "LiPo pouch - heat 0.04 W when charging"),
    ("lipo_503035", "protection PCM"): ("fr4_2", "Battery protection PCM"),
    ("usb_c_hro_type_c_31_m_12", "USB-C shell"): ("steel", "USB-C J1"),
    ("sw_slide_ck_js202011jcqn", "body"): ("steel", "Power switch SW1 body"),
    ("sw_slide_ck_js202011jcqn", "actuator"): ("nylon", "Power switch SW1 actuator"),
    ("sw_tact_xkb_ts1187a", "body"): ("steel", "Button body (SW2-SW5)"),
    ("sw_tact_xkb_ts1187a", "plunger"): ("nylon", "Button plunger (SW2-SW5)"),
    ("jst_ph_s2b_ph_sm4_tb", "housing"): ("nylon", "Battery connector J2"),
    ("J3", None): ("steel", "microSD socket J3"),
    ("U2", None): ("mold", "SE050 U2"),
    ("U3", None): ("mold", "Charger U3 - heat 0.65 W when charging"),
}
FUSION_DIR = os.path.join(OUT, "fusion", "TredderSim")


def fusion_package(boxes, sim_step):
    # KiCad's board body is thinner than 1.6 mm (copper/mask are left off), so the board is
    # matched as the largest body instead of by size
    entries = [{"largest": True, "dims": [0, 0, 0], "label": "Main board (4-layer FR4)", "material": "fr4_4"}]
    models = [gen_step_models.lcd(True), gen_step_models.lipo(), gen_step_models.usb_c(), gen_step_models.slide(),
              gen_step_models.tact(), gen_step_models.jst(), gen_step_models.pico_sim()]
    for s in models:
        for name, dims in s.boxes:
            mat, label = BODIES[(s.name, name)]
            entries.append({"dims": sorted(dims), "label": label, "material": mat})
    for ref, s in boxes:
        mat, label = BODIES[(ref, None)]
        entries.append({"dims": sorted(s.boxes[0][1]), "label": label, "material": mat})
    for i, e in enumerate(entries[1:], 1):   # the script matches bodies by size: sizes must be unique
        for f in entries[i + 1:]:
            if all(abs(x - y) < 0.1 for x, y in zip(e["dims"], f["dims"])) and e["label"] != f["label"]:
                sys.exit(f"Fusion package: '{e['label']}' and '{f['label']}' have the same size")
    for e in entries:
        e["dims"] = [round(v, 3) for v in e["dims"]]
    if os.path.isdir(FUSION_DIR):
        shutil.rmtree(FUSION_DIR)
    os.makedirs(FUSION_DIR)
    shutil.copy(os.path.join(HERE, "fusion", "TredderSim.py"), FUSION_DIR)
    shutil.copy(sim_step, FUSION_DIR)
    shutil.copy(os.path.join(OUT, "FUSION_SIM.md"), FUSION_DIR)
    with open(os.path.join(FUSION_DIR, "bodies.json"), "w", encoding="utf8") as fh:
        json.dump({"step": os.path.basename(sim_step), "f3d": "tredder-revA-sim.f3d",
                   "materials": MATERIALS, "bodies": entries}, fh, indent=1)
    with open(os.path.join(FUSION_DIR, "TredderSim.manifest"), "w", encoding="utf8") as fh:
        json.dump({"autodeskProduct": "Fusion", "type": "script", "author": "tredder",
                   "description": {"": "Builds the tredder rev A simulation model and saves tredder-revA-sim.f3d"},
                   "supportedOS": "windows|mac", "editEnabled": True}, fh, indent=1)
    z = shutil.make_archive(os.path.join(OUT, "tredder-fusion-TredderSim"), "zip", os.path.dirname(FUSION_DIR), "TredderSim")
    print("wrote", os.path.relpath(z, HW), f"({len(entries)} body types)")


def main():
    gen_step_models.main()
    os.makedirs(OUT, exist_ok=True)
    step(PCB, os.path.join(OUT, "tredder-revA.step"))
    tmp = tempfile.mkdtemp(prefix="tredder-mech-")
    try:
        board, boxes = sim_board(tmp)
        sim_step = os.path.join(OUT, "tredder-revA-sim.step")
        step(board, sim_step, SIM_REFS)
        fusion_package(boxes, sim_step)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
