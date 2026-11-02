"""
BT1: the LiPo cell behind the board. It is not a footprint: the cell is not soldered,
it plugs into J2 with its JST-PH lead, and a board part with no schematic symbol makes
Fusion (and other EDA importers) report the schematic and board as inconsistent.
So the battery is:
  - an outline + label on B.Fab (board drawings, for the layout and the case),
  - a second 3D model on J2, placed under the board where the cell sits, so the
    3D view and the STEP exports (Fusion) still show it.

Cell: 1S LiPo "503035" (5.0 x 30 x 35 mm), 500 mAh, WITH protection PCM,
JST-PH 2.0 mm lead, pin 1 = BAT+ (check: many packs are wired reversed).
500 mAh matches the 500 mA charge current set by R5 (1 C).

Placement: behind the left part of the board only (antenna keep-out on the
right, hardware/README.md section 7), clear of the J4 header pins (x 4.5)
and the H1/H4 screw heads (y < 5.5).
"""
import math
import os

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = "${KIPRJMOD}/3d/lipo_503035.step"   # solid model: tools/gen_step_models.py

L, W, T = 35.0, 30.0, 5.0      # cell length (x), width (y), thickness
CX, CY = 25.0, 22.0             # centre, board mm (top view, same frame as gen_pcb PLACE)
TAPE = 0.5                      # foam tape between board and cell


LABEL = "BT1 LiPo 503035 500mAh (bottom, plugs into J2)"


def add(board, origin_mm):
    """Draw the cell outline and put its model on J2 (idempotent). origin_mm = board origin, as in gen_pcb."""
    old = board.FindFootprintByReference("BT1")      # older boards had BT1 as a board-only footprint
    if old:
        board.Remove(old)
    ox, oy = origin_mm
    cx, cy = ox + CX, oy + CY
    for d in list(board.GetDrawings()):                # previous outline and label
        if d.GetLayer() != pcbnew.B_Fab:
            continue
        if d.GetClass() == "PCB_TEXT" and d.GetText() == LABEL:
            board.Remove(d)
        elif d.GetClass() == "PCB_SHAPE" and d.GetShape() == pcbnew.SHAPE_T_RECT:
            c = d.GetBoundingBox().Centre()
            if abs(pcbnew.ToMM(c.x) - cx) < 0.01 and abs(pcbnew.ToMM(c.y) - cy) < 0.01:
                board.Remove(d)
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_RECT)
    s.SetStart(pcbnew.VECTOR2I(pcbnew.FromMM(cx - L / 2), pcbnew.FromMM(cy - W / 2)))
    s.SetEnd(pcbnew.VECTOR2I(pcbnew.FromMM(cx + L / 2), pcbnew.FromMM(cy + W / 2)))
    s.SetLayer(pcbnew.B_Fab)
    s.SetWidth(pcbnew.FromMM(0.12))
    board.Add(s)
    t = pcbnew.PCB_TEXT(board)
    t.SetText(LABEL)
    t.SetLayer(pcbnew.B_Fab)
    t.SetMirrored(True)
    t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(1.2), pcbnew.FromMM(1.2)))
    t.SetPosition(pcbnew.VECTOR2I(pcbnew.FromMM(cx), pcbnew.FromMM(cy)))
    board.Add(t)
    attach_model(board, (cx, cy))


def attach_model(board, centre_mm):
    """Add the cell model to J2, transformed so it sits under the board at centre_mm (page mm, y down)."""
    j2 = board.FindFootprintByReference("J2")
    rot = j2.GetOrientationDegrees()
    jx, jy = pcbnew.ToMM(j2.GetPosition().x), pcbnew.ToMM(j2.GetPosition().y)
    dx, dy = centre_mm[0] - jx, -(centre_mm[1] - jy)            # board -> 3D frame (y up)
    a = math.radians(-rot)                                     # undo the footprint rotation
    ox, oy = dx * math.cos(a) - dy * math.sin(a), dx * math.sin(a) + dy * math.cos(a)
    thick = pcbnew.ToMM(board.GetDesignSettings().GetBoardThickness())
    keep = [m for m in j2.Models() if "lipo_503035" not in m.m_Filename]
    files = [(m.m_Filename, m.m_Offset, m.m_Rotation) for m in keep]
    j2.Models().clear()
    for f, off, r in files:
        m = pcbnew.FP_3DMODEL()
        m.m_Filename, m.m_Offset, m.m_Rotation = f, off, r
        j2.Models().append(m)
    m = pcbnew.FP_3DMODEL()
    m.m_Filename = MODEL
    # flipped under the board (rotate 180 about x); the z turn keeps the PCM end toward J2.
    # Checked against the old BT1 footprint: pixel-identical bottom render.
    m.m_Offset = pcbnew.VECTOR3D(round(ox, 3), round(oy, 3), -thick)
    m.m_Rotation = pcbnew.VECTOR3D(180, 0, rot % 360)
    j2.Models().append(m)


if __name__ == "__main__":
    PCB = os.path.abspath(os.path.join(HERE, "..", "wallet.kicad_pcb"))
    b = pcbnew.LoadBoard(PCB)
    add(b, (100.0, 100.0))
    b.Save(PCB)
    print("battery outline + J2 model added")
