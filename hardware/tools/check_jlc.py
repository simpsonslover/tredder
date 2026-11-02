"""
JLCPCB limits KiCad 10's DRC does not enforce here (its hole-to-hole check does not fire in
this project, even at 3 mm): hole-edge to hole-edge spacing.

  PTH (component) hole to PTH hole  >= 0.45 mm   "Pad Hole-to-Hole Spacing"
  any other hole pair (via, NPTH)   >= 0.20 mm   "Via Hole-to-Hole Spacing"

Source: https://jlcpcb.com/capabilities/pcb-capabilities (multilayer, read 2026-11-01).
Everything else is checked by KiCad's DRC with wallet.kicad_pro + wallet.kicad_dru.

Run with KiCad's python:  & "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tools/check_jlc.py
Exit code 1 if anything is closer than the limit.
"""
import math
import os
import sys

import pcbnew

PCB = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "wallet.kicad_pcb"))
PTH_PTH, OTHER = 0.45, 0.20


def holes(board):
    out = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.GetDrillSize().x > 0:
                kind = "PTH" if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH else "NPTH"
                d = pcbnew.ToMM(min(p.GetDrillSize().x, p.GetDrillSize().y))
                out.append((kind, f"{fp.GetReference()}:{p.GetNumber() or '-'}", p.GetPosition(), d))
    for v in board.GetTracks():
        if v.GetClass() == "PCB_VIA":
            out.append(("via", f"via {v.GetNetname()}", v.GetPosition(), pcbnew.ToMM(v.GetDrillValue())))
    return out


def main(path=PCB):
    h = holes(pcbnew.LoadBoard(path))
    bad, worst = [], {}
    for i in range(len(h)):
        for j in range(i + 1, len(h)):
            (ka, na, pa, da), (kb, nb, pb, db) = h[i], h[j]
            gap = math.hypot(pcbnew.ToMM(pa.x - pb.x), pcbnew.ToMM(pa.y - pb.y)) - (da + db) / 2
            if gap <= 0:      # coincident: the same pad drawn twice (castellated / stacked pads)
                continue
            limit = PTH_PTH if ka == kb == "PTH" else OTHER
            kind = "PTH-PTH" if limit == PTH_PTH else "other"
            if kind not in worst or gap < worst[kind][0]:
                worst[kind] = (gap, na, nb)
            if gap < limit - 1e-6:
                bad.append(f"{na} - {nb}: {gap:.3f} mm < {limit} mm")
    for kind, (gap, a, b) in sorted(worst.items()):
        print(f"closest {kind} holes: {a} - {b}: {gap:.3f} mm")
    if bad:
        print("JLCPCB hole spacing FAIL:\n  " + "\n  ".join(bad))
        sys.exit(1)
    print(f"JLCPCB hole spacing OK ({len(h)} holes)")


if __name__ == "__main__":
    main()
