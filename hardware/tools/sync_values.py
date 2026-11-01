"""
Copy part values and fields (Value, MPN, LCSC, Description) from design.py
into the existing wallet.kicad_pcb WITHOUT touching placement or routing.

Use after value-only changes (resistor/capacitor values, part numbers).
Footprint or connection changes need gen_pcb.py (+ re-route) or F8 in KiCad.

    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" sync_values.py
"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import design as D  # noqa: E402

PCB = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "wallet.kicad_pcb"))


def main():
    board = pcbnew.LoadBoard(PCB)
    changed = []
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        p = D.P.get(ref)
        if not p:
            continue
        want_fp = p["footprint"].split(":")[1]
        if str(fp.GetFPID().GetLibItemName()) != want_fp:
            raise SystemExit(f"{ref}: footprint changed ({fp.GetFPID().GetLibItemName()} -> {want_fp}); "
                             "that needs gen_pcb.py / re-route, not sync_values.py")
        if fp.GetValue() != p["value"]:
            changed.append(f"{ref}: {fp.GetValue()} -> {p['value']}")
            fp.SetValue(p["value"])
        for k, v in (("MPN", p["mpn"]), ("LCSC", p["lcsc"]), ("Description", p["note"])):
            fp.SetField(k, v)
            if k != "Description":
                fp.GetField(k).SetVisible(False)
    board.Save(PCB)
    print("\n".join(changed) or "no value changes")


if __name__ == "__main__":
    main()
