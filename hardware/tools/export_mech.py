"""
Export the board as STEP for Fusion (or any MCAD tool) into hardware/mech/.

  tredder-revA.step       everything, detailed: enclosure design (Phase 4), fit checks
  tredder-revA-sim.step   simulation-ready: board + the parts that matter mechanically
                          or thermally, each a few clean solids that do not overlap
                          (the Pico 2 W and the display get simplified models)

Run with KiCad's python:  & "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tools/export_mech.py
"""
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
             "U2": (0.6, (0.1, 0.1, 0.1), None),      # OPTIGA, USON-10
             "U3": (1.1, (0.1, 0.1, 0.1), None)}      # MCP73831 charger, SOT-23-5 (heat source while charging)
SIM_REFS = ["A1", "J1", "J2", "J3", "J4", "SW1", "SW2", "SW3", "SW4", "SW5", "U2", "U3", "BT1"]


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
    b = pcbnew.LoadBoard(PCB)
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
            files = [path]
        fp.Models().clear()
        for f in files:
            m = pcbnew.FP_3DMODEL()
            m.m_Filename = f
            fp.Models().append(m)
    out = os.path.join(tmp, "wallet_sim.kicad_pcb")
    b.Save(out)
    return out


def main():
    gen_step_models.main()
    os.makedirs(OUT, exist_ok=True)
    step(PCB, os.path.join(OUT, "tredder-revA.step"))
    tmp = tempfile.mkdtemp(prefix="tredder-mech-")
    try:
        step(sim_board(tmp), os.path.join(OUT, "tredder-revA-sim.step"), SIM_REFS)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    main()
