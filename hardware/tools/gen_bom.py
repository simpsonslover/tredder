"""
Generate BOMs from design.py.

  bom/wallet_bom_full.csv   every part, with MPN, LCSC, notes and a VERIFY flag
  bom/wallet_bom_jlc.csv    JLCPCB assembly format (only parts JLC can place)

Parts JLC can't source (Pico 2 W, OPTIGA, display, etc.) are kept out of the
JLC file and listed as "hand-place" in the full BOM.
"""
import csv
import os
from collections import OrderedDict

import design as D

OUT = os.path.join(os.path.dirname(__file__), "..", "bom")
HAND = {"A1", "U2", "J4", "SW1", "J3", "J2", "F1"}  # source yourself / hand-solder (see notes)


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
            source = "hand / other distributor" if set(refs) & HAND else ("none" if p["dnp"] else "JLCPCB")
            verify = "" if (p["lcsc_ok"] or p["dnp"]) else "YES - LCSC number / stock / pinout"
            notes = "; ".join(sorted({D.P[r]["note"] for r in refs if D.P[r]["note"]}))
            w.writerow([len(refs), " ".join(refs), val, mpn, lcsc, fp.split(":")[1], populate, source, verify, notes])

    with open(os.path.join(OUT, "wallet_bom_jlc.csv"), "w", newline="", encoding="utf8") as fh:
        w = csv.writer(fh)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (val, fp, mpn, lcsc), refs in groups.items():
            refs = [r for r in refs if not D.P[r]["dnp"] and r not in HAND]
            if refs and lcsc:
                w.writerow([f"{val} {mpn}".strip(), ",".join(refs), fp.split(":")[1], lcsc])
    print("bom written to", os.path.abspath(OUT))


if __name__ == "__main__":
    main()
