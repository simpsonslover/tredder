"""
One zip with everything needed to make tredder rev A: fab/tredder-revA-complete.zip

  1_order_JLCPCB/      upload these (board + full assembly), with the step-by-step guide
  2_other_factories/   PCBWay-style BOM and KiCad's plain placement file
  3_buy_separately/    shopping list: display, battery, screws, microSD... (not on the board)
  4_checks_and_docs/   schematic PDF, renders, fab notes, independent Gerber check, hardware README
  5_3d_models/         STEP of the assembled board (enclosure) and the simulation version
  6_kicad_project/     the editable design (open wallet.kicad_pro in KiCad 10)
  7_fusion/            Fusion electronics import copy + simulation script package

Runs export_fab.py first (DRC, Gerber check, corrected placement), then re-renders the
board and the schematic so nothing in the zip is stale.

Run with KiCad's python:  & "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tools/export_all.py
"""
import os
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.abspath(os.path.join(HERE, ".."))
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
PY = sys.executable
NAME = "tredder-revA"
OUT = os.path.join(HW, "fab", f"{NAME}-complete.zip")


def run(*args):
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"failed: {' '.join(args)}\n{r.stdout[-3000:]}{r.stderr[-3000:]}")
    return r.stdout


def refresh():
    print(run(PY, os.path.join(HERE, "export_fab.py")).strip().splitlines()[-1])
    print(run(PY, os.path.join(HERE, "export_mech.py")).strip().splitlines()[-1])
    print(run(PY, os.path.join(HERE, "export_fusion_ecad.py")).strip().splitlines()[-1])
    pcb, sch = os.path.join(HW, "wallet.kicad_pcb"), os.path.join(HW, "wallet.kicad_sch")
    run(CLI, "sch", "export", "pdf", "-o", os.path.join(HW, "wallet_schematic.pdf"), sch)
    for name, args in (("render_top", ["--side", "top"]), ("render_bottom", ["--side", "bottom"]),
                       ("render_iso", ["--side", "top", "--rotate", "-50,0,30", "--perspective"])):
        run(CLI, "pcb", "render", *args, "-w", "1600", "-h", "1100", "--quality", "high",
            "-o", os.path.join(HW, name + ".png"), pcb)
    print("schematic PDF and renders refreshed")


README = f"""tredder rev A: everything needed to make the board
=====================================================

1_order_JLCPCB/       Order the board, fully assembled, at jlcpcb.com.
                      Follow 1_order_JLCPCB/READ_ME_FIRST.txt step by step.
                      The placement file (CPL) is already corrected for JLCPCB's
                      own footprints (rotations measured per part).

2_other_factories/    For PCBWay or another assembler: same Gerbers, a BOM by
                      manufacturer part number, and KiCad's plain placement file.

3_buy_separately/     PARTS_LIST.md: what is NOT on the board and you buy yourself
                      (2.0in ST7789 display module, 503035 LiPo with protection and
                      JST-PH lead, foam tape, M2 screws, microSD card, USB-C cable).

4_checks_and_docs/    Schematic (PDF), board renders, FAB_NOTES.md, the independent
                      check of the Gerber files (gerber_drc.md), and the hardware
                      README with the checklist before ordering (section 6) and the
                      bring-up / test plan (section 8).

5_3d_models/          STEP models: the assembled board with display and battery (for
                      designing the case) and the simplified simulation version.

6_kicad_project/      The editable design. Open wallet.kicad_pro in KiCad 10.

7_fusion/             For Autodesk Fusion: the electronics import copy (KiCad 9
                      format) and the simulation script package (see FUSION_SIM.md).

8_breadboard_prototype/  Build and test the whole circuit on a breadboard first
                      (same Pico pins as the PCB, so the same firmware runs).

BEFORE YOU PAY (hardware README section 6):
  1. Buy the display module first; its pin order must be
     GND VCC SCL SDA RES DC CS BLK on J4, about 60 x 36 mm.
  2. Check the SE050 (U2) footprint against NXP's SOT1969-1 package drawing.
  3. Check the DPDT power switch commons (pins 2 and 5) and the microSD
     card-detect direction against their datasheets.
  4. Look over the routed tracks yourself in KiCad.
  5. On JLCPCB: 4 layers, every BOM line in stock, and look at the placement
     preview (pin 1 / polarity of U1, U3, Q1-Q5, D1).
  6. Order the NXP SE050C2 (U2) yourself from Mouser / Digi-Key / Farnell:
     SE050C2HQ1/Z01SDZ (buy spares). JLCPCB leaves its pads bare; you solder
     it (0.4 mm pitch: flux, solder paste + hot air or a hot plate).
  7. Before plugging the battery in: check its lead polarity (pin 1 = BAT+).
"""


def build():
    j = lambda *p: os.path.join(HW, *p)
    files = [
        ("1_order_JLCPCB/READ_ME_FIRST.txt", None),     # taken from the JLCPCB bundle below
        ("1_order_JLCPCB/tredder-revA-gerbers.zip", j("fab", f"{NAME}-gerbers.zip")),
        ("1_order_JLCPCB/tredder-revA-bom-jlc.csv", j("fab", f"{NAME}-bom-jlc.csv")),
        ("1_order_JLCPCB/tredder-revA-cpl-jlc.csv", j("fab", f"{NAME}-cpl-jlc.csv")),
        ("2_other_factories/tredder-revA-gerbers.zip", j("fab", f"{NAME}-gerbers.zip")),
        ("2_other_factories/tredder-revA-bom-pcbway.csv", j("fab", f"{NAME}-bom-pcbway.csv")),
        ("2_other_factories/tredder-revA-cpl.csv", j("fab", f"{NAME}-cpl.csv")),
        ("3_buy_separately/PARTS_LIST.md", j("bom", "PARTS_LIST.md")),
        ("3_buy_separately/wallet_bom_full.csv", j("bom", "wallet_bom_full.csv")),
        ("4_checks_and_docs/wallet_schematic.pdf", j("wallet_schematic.pdf")),
        ("4_checks_and_docs/render_top.png", j("render_top.png")),
        ("4_checks_and_docs/render_bottom.png", j("render_bottom.png")),
        ("4_checks_and_docs/render_iso.png", j("render_iso.png")),
        ("4_checks_and_docs/FAB_NOTES.md", j("fab", "FAB_NOTES.md")),
        ("4_checks_and_docs/gerber_drc.md", j("fab", "gerber_drc.md")),
        ("4_checks_and_docs/gerber_drc.png", j("fab", "gerber_drc.png")),
        ("4_checks_and_docs/hardware_README.md", j("README.md")),
        ("5_3d_models/tredder-revA.step", j("mech", f"{NAME}.step")),
        ("5_3d_models/tredder-revA-sim.step", j("mech", f"{NAME}-sim.step")),
        ("7_fusion/tredder-revA-fusion-ecad.zip", j("mech", f"{NAME}-fusion-ecad.zip")),
        ("7_fusion/tredder-fusion-TredderSim.zip", j("mech", "tredder-fusion-TredderSim.zip")),
        ("7_fusion/FUSION_SIM.md", j("mech", "FUSION_SIM.md")),
        ("8_breadboard_prototype/breadboard.png", j("breadboard", "breadboard.png")),
        ("8_breadboard_prototype/BREADBOARD.md", j("breadboard", "BREADBOARD.md")),
    ]
    for f in ("wallet.kicad_pro", "wallet.kicad_pcb", "wallet.kicad_sch", "wallet.kicad_sym", "wallet.kicad_dru",
              "fp-lib-table", "sym-lib-table", "jlcpcb-4L-real.edru"):
        files.append((f"6_kicad_project/{f}", j(f)))
    for d in ("wallet.pretty", "3d"):
        for name in sorted(os.listdir(j(d))):
            files.append((f"6_kicad_project/{d}/{name}", j(d, name)))

    with zipfile.ZipFile(j("fab", f"{NAME}-jlcpcb-order.zip")) as zf:
        readme_jlc = zf.read("READ_ME_FIRST.txt")
    missing = [src for _, src in files if src and not os.path.exists(src)]
    if missing:
        sys.exit("missing: " + ", ".join(missing))
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("READ_ME_FIRST.txt", README.replace("\n", "\r\n"))
        for arc, src in files:
            if src is None:
                zf.writestr(arc, readme_jlc)
            else:
                zf.write(src, arc)
    print(f"wrote {os.path.relpath(OUT, HW)}: {len(files) + 1} files, {os.path.getsize(OUT) / 1e6:.1f} MB")


if __name__ == "__main__":
    refresh()
    build()
