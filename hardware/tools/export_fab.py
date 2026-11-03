"""
Manufacturing package for PCBWay, JLCPCB or any other fab/assembler.

    python hardware/tools/export_fab.py          (needs KiCad 10's kicad-cli)

Writes hardware/fab/:
  tredder-revA-gerbers.zip     Gerber X2 (4 copper + mask/paste/silk/edge) + Excellon drill: upload this to the fab
  gerbers/                     the same files, unzipped
  tredder-revA-bom-pcbway.csv  assembly BOM, PCBWay layout (also fine for most assemblers)
  tredder-revA-bom-jlc.csv     assembly BOM, JLCPCB layout (LCSC part numbers)
  tredder-revA-cpl.csv         pick-and-place (centroid) file, top side, mm
  tredder-revA-cpl-jlc.csv     the same in JLCPCB's column names
  FAB_NOTES.md                 board spec to type into the order form + assembly notes

Refuses to export a board that has DRC errors or unrouted connections.
"""
import csv
import os
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.abspath(os.path.join(HERE, ".."))
PCB = os.path.join(HW, "wallet.kicad_pcb")
OUT = os.path.join(HW, "fab")
NAME = "tredder-revA"
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"

sys.path.insert(0, HERE)
import design as D  # noqa: E402
from gen_bom import GLOBAL, HAND, natural  # noqa: E402

LAYERS = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Paste", "B.Paste", "F.Silkscreen", "B.Silkscreen",
          "F.Mask", "B.Mask", "Edge.Cuts"]


def run(*args):
    r = subprocess.run([CLI, *args], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"kicad-cli {' '.join(args[:3])} failed:\n{r.stdout}\n{r.stderr}")
    return r.stdout


def drc_gate():
    rpt = os.path.join(OUT, "drc.rpt")
    out = run("pcb", "drc", "--refill-zones", "--schematic-parity", "--severity-error", "-o", rpt, PCB)
    txt = open(rpt, encoding="utf8", errors="replace").read()
    kinds = re.findall(r"^\[([a-z_]+)\]", txt, re.M)
    known = {"items_not_allowed", "starved_thermal"}  # documented as benign in hardware/README.md
    bad = [k for k in kinds if k not in known]
    parity = re.search(r"Found (\d+) schematic parity", out)
    if bad or not parity or parity.group(1) != "0":
        raise SystemExit(f"NOT exporting: DRC problems {sorted(set(bad))} / parity {parity.group(1) if parity else '?'} "
                         f"(see {rpt}). Route the board and fix DRC first.")
    os.remove(rpt)
    print(f"DRC ok ({len(kinds)} known/benign items)")
    # JLCPCB hole spacing, which KiCad 10's DRC does not check here (tools/check_jlc.py)
    import check_jlc
    check_jlc.main(PCB)


def gerber_gate():
    """Independent check of the files the fab receives (tools/gerber_drc.py, needs the system
    python with numpy, scipy, pillow and pygerber; KiCad's python does not have them)."""
    py = shutil.which("python") or shutil.which("py")
    if not py:
        raise SystemExit("NOT exporting: no system python for tools/gerber_drc.py")
    r = subprocess.run([py, os.path.join(os.path.dirname(os.path.abspath(__file__)), "gerber_drc.py"),
                        os.path.join(OUT, "gerbers")], capture_output=True, text=True)
    print((r.stdout.strip().splitlines() or ["(no output)"])[-1])
    if r.returncode != 0:
        raise SystemExit("NOT exporting: the Gerber check failed, see fab/gerber_drc.md\n"
                         + r.stdout[-2000:] + r.stderr[-2000:])


def gerbers():
    g = os.path.join(OUT, "gerbers")
    shutil.rmtree(g, ignore_errors=True)
    os.makedirs(g)
    run("pcb", "export", "gerbers", "-o", g + os.sep, "-l", ",".join(LAYERS), "--subtract-soldermask",
        "--check-zones", PCB)
    run("pcb", "export", "drill", "-o", g + os.sep, "--format", "excellon", "--excellon-separate-th",
        "--excellon-units", "mm", "--generate-map", "--map-format", "gerberx2", PCB)
    z = os.path.join(OUT, f"{NAME}-gerbers.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        for f in sorted(os.listdir(g)):
            zf.write(os.path.join(g, f), f)
    print(f"gerbers: {len(os.listdir(g))} files -> {z}")


def placement():
    """Centroids of every part to be placed (top side; DNP and board-only parts excluded)."""
    raw = os.path.join(OUT, "pos_raw.csv")
    run("pcb", "export", "pos", "-o", raw, "--format", "csv", "--units", "mm", "--side", "front",
        "--exclude-dnp", "--use-drill-file-origin", PCB)
    rows = list(csv.DictReader(open(raw, encoding="utf8")))
    os.remove(raw)
    rows = [r for r in rows if r["Ref"] in D.P and not D.P[r["Ref"]]["dnp"]]
    rows.sort(key=lambda r: natural(r["Ref"]))
    with open(os.path.join(OUT, f"{NAME}-cpl.csv"), "w", newline="", encoding="utf8") as fh:
        w = csv.writer(fh)
        w.writerow(["Designator", "Mid X (mm)", "Mid Y (mm)", "Layer", "Rotation", "Assembly"])
        for r in rows:
            w.writerow([r["Ref"], r["PosX"], r["PosY"], "Top", r["Rot"],
                        "hand-solder / customer-supplied" if r["Ref"] in HAND else "machine"])
    # JLCPCB places with its own (EasyEDA) footprints: measured rotation/position corrections
    import jlc_rotation
    import pcbnew
    fix = jlc_rotation.corrections(pcbnew.LoadBoard(PCB), D.P)
    check = []
    with open(os.path.join(OUT, f"{NAME}-cpl-jlc.csv"), "w", newline="", encoding="utf8") as fh:
        w = csv.writer(fh)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for r in rows:
            if r["Ref"] in HAND:
                continue
            x, y, rot = float(r["PosX"]), float(r["PosY"]), float(r["Rot"])
            th, t, note = fix.get(r["Ref"], (None, None, "no LCSC part"))
            if th is None:
                check.append(f"{r['Ref']}: {note}")
            else:
                x, y, rot = x + t[0], y + t[1], rot + th
            w.writerow([r["Ref"], f"{x:.4f}mm", f"{y:.4f}mm", "Top", f"{rot % 360:.1f}"])
    changed = sorted(f for f, (th, t, _) in fix.items() if th is not None and (th or abs(t[0]) + abs(t[1]) > 0.01))
    print(f"pick-and-place: {len(rows)} parts; JLCPCB rotation/position corrected for {', '.join(changed)}")
    print("  check in JLCPCB's placement preview: " + ("; ".join(check) if check else "none"))
    placement.check = check
    return rows


def boms():
    groups = {}
    for ref in sorted(D.P, key=natural):
        p = D.P[ref]
        if p["dnp"]:
            continue
        groups.setdefault((p["footprint"], p["mpn"], p["lcsc"]), []).append(ref)   # one line per physical part
    with open(os.path.join(OUT, f"{NAME}-bom-pcbway.csv"), "w", newline="", encoding="utf8") as fh:
        w = csv.writer(fh)
        w.writerow(["Item #", "Designator", "Qty", "Manufacturer Part #", "Description / Value", "Package",
                    "Type", "Your Instructions / Notes"])
        for i, ((fp, mpn, lcsc), refs) in enumerate(groups.items(), 1):
            vals = sorted({D.P[r]["value"] for r in refs})
            val = vals[0] if len(vals) == 1 else " / ".join(vals)
            pkg = fp.split(":")[1]
            tht = any(k in pkg for k in ("PinSocket", "JST_PH", "MountingHole"))
            note = []
            if set(refs) & GLOBAL:
                note.append("please source by MPN from an authorised distributor (not stocked at LCSC)")
            if set(refs) & HAND:
                note.append("DO NOT FIT: customer fits this part")
            if not D.P[refs[0]]["lcsc_ok"]:
                note.append(f"LCSC {lcsc or 'n/a'} unverified")
            elif lcsc:
                note.append(f"LCSC {lcsc}")
            w.writerow([i, ",".join(refs), len(refs), mpn, val, pkg, "THT" if tht else "SMD", "; ".join(note)])
    shutil.copy(os.path.join(HW, "bom", "wallet_bom_jlc.csv"), os.path.join(OUT, f"{NAME}-bom-jlc.csv"))
    print(f"boms: {len(groups)} line items")


def notes(n_place):
    t = f"""# {NAME}: ordering notes

Generated by `hardware/tools/export_fab.py`. Upload **`{NAME}-gerbers.zip`** for the bare board.
For assembly, also upload a BOM (`-bom-pcbway.csv` or `-bom-jlc.csv`) and the pick-and-place file
(`-cpl.csv` or `-cpl-jlc.csv`).

## Board (order form)

| Field | Value |
|---|---|
| Layers | **4** |
| Size | 85.6 x 54.0 mm |
| Quantity | 5 (minimum at most fabs) |
| Material / thickness | FR-4 TG150, **1.6 mm** |
| Stack-up | L1 F.Cu signal / L2 In1.Cu GND plane / L3 In2.Cu +3V3 plane / L4 B.Cu signal + GND. Fab's standard 4-layer stack-up is fine (no impedance control needed) |
| Min track / spacing | 0.15 mm / 0.15 mm (6/6 mil); design rule floor 0.127 mm |
| Min hole | 0.3 mm (vias 0.6/0.3 mm, power vias 0.8/0.4 mm) |
| Via process | **Plugged** (JLCPCB's name for ink-plugged, mask-covered vias; was "tented"). Not "Untented"; filled & capped is not needed (no via-in-pad) |
| Surface finish | **ENIG** (flat pads for the castellated Pico 2 W and the 0.4 mm-pitch SE050) |
| Copper | 1 oz outer, 0.5 oz inner |
| Solder mask / silk | any colour (green / white is cheapest) |
| Castellated holes | **no** (the Pico 2 W is the castellated part, the board has plain pads) |
| Edge connector / gold fingers | no |
| Remove order number | yes, or "specify location": there is no free area, so choose "remove" |

## Files

| File | Use |
|---|---|
| `{NAME}-gerbers.zip` | Gerber X2: F.Cu (GTL), In1.Cu (G1), In2.Cu (G2), B.Cu (GBL), masks, paste, silk, Edge.Cuts (GM1); Excellon PTH + NPTH drill (mm) and drill maps |
| `{NAME}-bom-pcbway.csv` | assembly BOM in PCBWay's columns |
| `{NAME}-bom-jlc.csv` | assembly BOM in JLCPCB's columns (only parts JLC can source) |
| `{NAME}-cpl.csv` | centroid file, all {n_place} placed parts, top side, mm, column "Assembly" says machine / hand |
| `{NAME}-cpl-jlc.csv` | centroid file in JLCPCB's columns (machine-placed parts only) |

## Assembly

- **Top side only.** Nothing is placed on the bottom (the battery is stuck on later).
- **JLCPCB assembles everything except U2.** Choose **Standard PCBA** (J4 is a through-hole socket; the
  Pico 2 W C42394205 is an extended part). Upload `-bom-jlc.csv` + `-cpl-jlc.csv`.
- **U2 (NXP SE050C2) is not on the JLCPCB order:** it is left out of the JLC BOM and placement
  file, so its pads arrive bare. Buy SE050C2HQ1/Z01SDZ from Mouser / Digi-Key / Farnell (NXP-authorised)
  and solder it: 0.4 mm pitch, centre pad to GND; flux, solder paste, hot air or a hot plate.
- **Not assembled:** the display module and the battery: they plug in (J4, J2).
- **Do not fit:** H1-H4 (screw holes).
- **Production option:** leave R3/R4 unfitted for a charge-only USB port (no USB data / BOOTSEL attack surface).
- **Rotations:** `-cpl-jlc.csv` is already corrected for JLCPCB's own footprints (measured per part
  by `tools/jlc_rotation.py`: e.g. SOT-23 +180, SOT-23-5/-6 +270, LED +180, J4 +270 and moved
  8.89 mm). The plain `-cpl.csv` keeps KiCad's values for other assemblers. Still look at the
  preview, especially pin 1 of U1, U3, Q1-Q5 and the LED's polarity mark.

## Before ordering

1. The display module's pin order and outline must match J4 (hardware/README.md section 6.2).
2. Check stock for every BOM line in JLCPCB's parts list.
3. This is rev A: order the minimum quantity and bring it up with hardware/README.md section 8 first.
"""
    open(os.path.join(OUT, "FAB_NOTES.md"), "w", encoding="utf8").write(t)


def main():
    if not os.path.exists(CLI):
        raise SystemExit(f"kicad-cli not found at {CLI}")
    os.makedirs(OUT, exist_ok=True)
    drc_gate()
    gerbers()
    gerber_gate()
    rows = placement()
    boms()
    notes(len(rows))
    bundle()
    print("fab package ->", OUT)


def bundle():
    """One zip with everything for a JLCPCB order with full assembly. The Gerber zip stays a separate zip
    inside it: JLCPCB builds the board from the Gerber zip and takes the BOM / CPL in the assembly step."""
    howto = "\r\n".join([
        "JLCPCB order, full assembly (PCBA)",
        "==================================",
        "",
        f"1. Quote page: upload {NAME}-gerbers.zip  (do NOT unzip it)",
        "   Layers 4 | 1.6 mm | ENIG | Via covering: Plugged",
        "2. Same page, bottom: PCB Assembly ON -> Standard, Top side, Qty 2-5,",
        "   Tooling holes by JLCPCB, Confirm parts placement: Yes -> NEXT -> NEXT",
        f"3. Upload page:  BOM = {NAME}-bom-jlc.csv   CPL = {NAME}-cpl-jlc.csv",
        "   -> Process BOM & CPL",
        "4. U2 (NXP SE050C2) is NOT in these files: JLCPCB leaves its pads bare.",
        "   Buy SE050C2HQ1/Z01SDZ from Mouser / Digi-Key / Farnell and solder it yourself.",
        "5. Placement preview: rotations are already corrected for JLCPCB's footprints.",
        "   Still check pin 1 / polarity of U1, U3, Q1-Q5 and D1, then Save to Cart",
        "",
        "Details: FAB_NOTES.md",
        ""])
    z = os.path.join(OUT, f"{NAME}-jlcpcb-order.zip")
    with zipfile.ZipFile(z, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.writestr("READ_ME_FIRST.txt", howto)
        for f in (f"{NAME}-gerbers.zip", f"{NAME}-bom-jlc.csv", f"{NAME}-cpl-jlc.csv", "FAB_NOTES.md"):
            zf.write(os.path.join(OUT, f), f)
    print("order bundle ->", z)



if __name__ == "__main__":
    main()
