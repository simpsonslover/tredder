"""
Generate BOMs from design.py.

  bom/wallet_bom_full.csv   every part, with MPN, LCSC, notes and a VERIFY flag
  bom/wallet_bom_jlc.csv    JLCPCB assembly format (only parts JLC can place)
  bom/PARTS_LIST.md         complete shopping list: board parts + everything off the board

Parts JLC can't source (Pico 2 W, OPTIGA, display, etc.) are kept out of the
JLC file and listed as "hand-place" in the full BOM.
"""
import csv
import os
from collections import OrderedDict

import design as D

OUT = os.path.join(os.path.dirname(__file__), "..", "bom")
HAND = {"U2"}      # you buy and solder these (not on the JLCPCB order): the OPTIGA Trust M
GLOBAL = set()     # parts JLCPCB would buy by MPN ("global sourcing"): none


def natural(ref):
    i = len(ref.rstrip("0123456789"))
    return (ref[:i], int(ref[i:] or 0))


def main():
    os.makedirs(OUT, exist_ok=True)
    groups = OrderedDict()
    for ref in sorted(D.P, key=natural):
        p = D.P[ref]
        key = (p["value"], p["footprint"], p["mpn"], p["lcsc"])
        groups.setdefault(key, []).append(ref)

    with open(os.path.join(OUT, "wallet_bom_full.csv"), "w", newline="", encoding="utf8") as fh:
        w = csv.writer(fh)
        w.writerow(["Qty", "Designators", "Value", "MPN", "LCSC", "Footprint", "Populate",
                    "Source", "VERIFY before ordering", "Notes"])
        for (val, fp, mpn, lcsc), refs in groups.items():
            p = D.P[refs[0]]
            populate = "DNP" if p["dnp"] else "yes"
            source = ("none" if p["dnp"] else "you: buy from Mouser / Digi-Key and solder" if set(refs) & HAND
                      else "JLCPCB global sourcing (MPN)" if set(refs) & GLOBAL else "JLCPCB / LCSC")
            verify = "" if (p["lcsc_ok"] or p["dnp"]) else "YES - LCSC number / stock / pinout"
            notes = "; ".join(sorted({D.P[r]["note"] for r in refs if D.P[r]["note"]}))
            w.writerow([len(refs), " ".join(refs), val, mpn, lcsc, fp.split(":")[1], populate, source, verify, notes])

    # One line per physical part (footprint + MPN + LCSC). Grouping by value too would split
    # identical parts with different labels (the four buttons are "UP", "DOWN", ...) into
    # several lines matched to the same LCSC part, which JLCPCB flags as an error.
    jlc = OrderedDict()
    for (val, fp, mpn, lcsc), refs in groups.items():
        refs = [r for r in refs if not D.P[r]["dnp"] and r not in HAND]
        if refs and (lcsc or set(refs) & GLOBAL):
            e = jlc.setdefault((fp, mpn, lcsc), {"vals": [], "refs": []})
            e["vals"].append(val)
            e["refs"] += refs
    with open(os.path.join(OUT, "wallet_bom_jlc.csv"), "w", newline="", encoding="utf8") as fh:
        w = csv.writer(fh)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (fp, mpn, lcsc), e in jlc.items():
            val = e["vals"][0] if len(set(e["vals"])) == 1 else ""
            refs = sorted(e["refs"], key=natural)
            # blank LCSC: JLC matches the MPN and offers global sourcing / consignment
            w.writerow([f"{val} {mpn}".strip() if lcsc else mpn, ",".join(refs), fp.split(":")[1], lcsc])
    write_parts_list(groups)
    print("bom written to", os.path.abspath(OUT))


# Everything needed to build one device that is NOT a part on the PCB (design.py only knows the board).
OFF_BOARD = [
    ("Display module", 1, "2.0\" IPS TFT, 240x320, ST7789V controller, 4-wire SPI, 8-pin 2.54 mm male header",
     "AliExpress / Waveshare / similar",
     "Plugs into J4. Pin order must be GND VCC SCL SDA RES DC CS BLK, outline about 60 x 36 mm, "
     "or edit NETS in design.py to match. Buy it BEFORE ordering boards (README section 6.2)."),
    ("Battery (BT1)", 1, "1S LiPo 503035, 3.7 V 500 mAh, 5.0 x 30 x 35 mm, WITH protection PCM, JST-PH 2.0 mm 2-pin lead",
     "battery shop / AliExpress",
     "Behind the left half of the board (tools/add_battery.py). CHECK the lead polarity: pin 1 = BAT+. "
     "500 mAh matches the 500 mA charge current (1 C)."),
    ("Foam tape", 1, "double-sided foam tape, 0.5 mm, about 30 x 25 mm", "any",
     "Holds the cell to the back of the board."),
    ("Screws", 4, "M2 low-head (wafer / pan) screws, length to suit the case", "any",
     "H1-H4. H1 and H4 sit under the display module: low head only."),
    ("microSD card", 1, "microSD, small capacity (1-8 GB), industrial / high-endurance grade", "any reputable brand",
     "For the encrypted backup. Treat it as untrusted media; it is only powered during a backup."),
    ("USB-C cable", 1, "USB-C, data-capable for the prototype", "any",
     "Charging and (prototype only) firmware loading. Production can be charge-only (R3/R4 not fitted)."),
]
OPTIONAL = [
    ("Debug probe", 1, "Raspberry Pi Debug Probe (or any CMSIS-DAP probe)", "Raspberry Pi resellers",
     "Optional: SWD debugging via the Pico's underside pads D1-D3 (wires soldered through the holes "
     "from the back). Firmware loads over USB-C without it."),
    ("Enclosure", 1, "tamper-evident case (Phase 4, not designed yet)", "3D print", "Leaves room for the 5.5 mm cell behind the board."),
]


def write_parts_list(groups):
    rows_jlc, rows_hand, rows_dnp = [], [], []
    for (val, fp, mpn, lcsc), refs in groups.items():
        p = D.P[refs[0]]
        verify = "" if (p["lcsc_ok"] or p["dnp"]) else " VERIFY"
        row = (len(refs), " ".join(refs), val, mpn, lcsc, fp.split(":")[1], verify)
        if p["dnp"]:
            rows_dnp.append(row)
        elif set(refs) & (GLOBAL | HAND):
            rows_hand.append(row + ("; ".join(sorted({D.P[r]["note"] for r in refs if D.P[r]["note"]})),))
        else:
            rows_jlc.append(row)
    n_parts = sum(r[0] for r in rows_jlc + rows_hand)
    L = ["# Parts list: one tredder (rev A)", "",
         "Generated by `hardware/tools/gen_bom.py` from `design.py` (do not edit by hand). "
         "Machine-readable versions: `wallet_bom_full.csv`, `wallet_bom_jlc.csv`.", "",
         "**VERIFY** = the LCSC number, stock or pinout was not confirmed: look it up before ordering "
         "(hardware/README.md section 6).", "",
         "## Summary", "",
         "| What | Where | Line items | Parts |", "|---|---|---|---|",
         f"| PCB, 4-layer | JLCPCB (or any fab) | 1 | 1 |",
         f"| Parts JLCPCB sources (LCSC) and assembles | JLCPCB assembly | {len(rows_jlc)} | {sum(r[0] for r in rows_jlc)} |",
         f"| Parts you buy and solder (not on the JLCPCB order) | Mouser / Digi-Key | {len(rows_hand)} | {sum(r[0] for r in rows_hand)} |",
         f"| Off-board parts | various | {len(OFF_BOARD)} | {sum(r[1] for r in OFF_BOARD)} |",
         f"| Not fitted (pads, holes) | - | {len(rows_dnp)} | 0 |", "",
         f"{n_parts} components on the board: JLCPCB assembles all of them **except U2, the OPTIGA Trust M, "
         "which you buy and solder** (section 3). The display module and the battery plug in.", "",
         "## 1. Circuit board", "",
         "| Item | Spec |", "|---|---|",
         "| Size | 85.6 x 54.0 mm (credit card), 3 mm corner radius |",
         "| Layers | 4: F.Cu signals / In1 GND / In2 +3V3 / B.Cu signals + GND |",
         "| Thickness | 1.6 mm FR-4 |",
         "| Finish | ENIG preferred (flat pads for the castellated Pico 2 W and the 0.5 mm-pitch OPTIGA) |",
         "| Files | `wallet.kicad_pcb`: export Gerbers + drill from KiCad |",
         "| Assembly | JLCPCB **Standard PCBA**, top side only: SMT + one through-hole part (J4) |", "",
         "## 2. JLCPCB sources and assembles (`wallet_bom_jlc.csv`)", "",
         "| Qty | Designators | Value | Part number | LCSC | Package |", "|---|---|---|---|---|---|"]
    for q, refs, val, mpn, lcsc, fp, v in rows_jlc:
        L.append(f"| {q} | {refs} | {val} | {mpn} | {lcsc}{v} | {fp} |")
    L += ["", "## 3. You buy and solder (not on the JLCPCB order)", "",
          "| Qty | Designators | Part | Part number | LCSC | Notes |", "|---|---|---|---|---|---|"]
    for q, refs, val, mpn, lcsc, fp, v, note in rows_hand:
        L.append(f"| {q} | {refs} | {val} | {mpn} | {lcsc or '-'}{v} | {note} |")
    L += ["", "## 4. Off the board", "", "| Item | Qty | Spec | Where | Notes |", "|---|---|---|---|---|"]
    for item, q, spec, where, note in OFF_BOARD:
        L.append(f"| {item} | {q} | {spec} | {where} | {note} |")
    L += ["", "## 5. Not fitted (footprints only)", "", "| Designators | What |", "|---|---|"]
    for q, refs, val, mpn, lcsc, fp, v in rows_dnp:
        L.append(f"| {refs} | {val}: {mpn} |")
    L += ["", "## 6. Optional: development tools and Phase 4", "", "| Item | Qty | Spec | Where | Notes |", "|---|---|---|---|---|"]
    for item, q, spec, where, note in OPTIONAL:
        L.append(f"| {item} | {q} | {spec} | {where} | {note} |")
    L += ["", "## Before you order", "",
          "1. Buy the display module first and check its pin order and outline against J4.",
          "2. Check every **VERIFY** LCSC number and its stock.",
          "3. Check the battery lead polarity (pin 1 = BAT+) before plugging it in: a reversed pack destroys the charger.",
          "4. Order the OPTIGA Trust M (U2) from Mouser or Digi-Key: SLS32AIA010MKUSON10XTMB2 (or ...XTMA2, or the "
          "-40..105 C SLS32AIA010ML...: same V3 chip and package). Buying it yourself from an authorised "
          "distributor also means you know where the chip your PIN protection depends on came from. Fitting it: "
          "3 x 3 mm, 0.5 mm pitch with a pad underneath (floating, for mechanical support): use solder paste and "
          "hot air or a hot plate (hardware/README.md section 8).",
          "5. Check JLCPCB's assembly preview (part rotations) before paying.", ""]
    with open(os.path.join(OUT, "PARTS_LIST.md"), "w", encoding="utf8") as fh:
        fh.write("\n".join(L))


if __name__ == "__main__":
    main()
