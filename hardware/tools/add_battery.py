"""
BT1: the LiPo cell behind the board, as a board-only mechanical footprint on the
bottom side (outline on B.Fab + a STEP model from gen_step_models.py) so renders and the Phase 4
enclosure see it. It has no pads: the cell plugs into J2 with its JST-PH lead.

Cell: 1S LiPo "503035" (5.0 x 30 x 35 mm), 500 mAh, WITH protection PCM,
JST-PH 2.0 mm lead, pin 1 = BAT+ (check: many packs are wired reversed).
500 mAh matches the 500 mA charge current set by R5 (1 C).

Placement: behind the left part of the board only (antenna keep-out on the
right, hardware/README.md section 7), clear of the J4 header pins (x 4.5)
and the H1/H4 screw heads (y < 5.5).
"""
import os

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
MODEL = "${KIPRJMOD}/3d/lipo_503035.step"   # solid model: tools/gen_step_models.py

L, W, T = 35.0, 30.0, 5.0      # cell length (x), width (y), thickness
CX, CY = 25.0, 22.0             # centre, board mm (top view, same frame as gen_pcb PLACE)
TAPE = 0.5                      # foam tape between board and cell


def add(board, origin_mm):
    """Add BT1 (idempotent). origin_mm = board origin on the page, as in gen_pcb."""
    old = board.FindFootprintByReference("BT1")
    if old:
        board.Remove(old)
    ox, oy = origin_mm
    fp = pcbnew.FOOTPRINT(board)
    fp.SetReference("BT1")
    fp.SetValue("LiPo 503035 500mAh (behind board, plugs into J2)")
    fp.SetAttributes(pcbnew.FP_BOARD_ONLY | pcbnew.FP_EXCLUDE_FROM_BOM | pcbnew.FP_EXCLUDE_FROM_POS_FILES)
    fp.Reference().SetVisible(False)
    fp.Value().SetVisible(False)
    board.Add(fp)
    pos = pcbnew.VECTOR2I(pcbnew.FromMM(ox + CX), pcbnew.FromMM(oy + CY))
    fp.SetPosition(pos)
    # outline on Fab (front first; flipping moves it to B.Fab)
    s = pcbnew.PCB_SHAPE(fp, pcbnew.SHAPE_T_RECT)
    s.SetStart(pcbnew.VECTOR2I(pos.x - pcbnew.FromMM(L / 2), pos.y - pcbnew.FromMM(W / 2)))
    s.SetEnd(pcbnew.VECTOR2I(pos.x + pcbnew.FromMM(L / 2), pos.y + pcbnew.FromMM(W / 2)))
    s.SetLayer(pcbnew.F_Fab)
    s.SetWidth(pcbnew.FromMM(0.12))
    fp.Add(s)
    t = pcbnew.PCB_TEXT(fp)
    t.SetText("BT1 LiPo 503035 500mAh (bottom)")
    t.SetPosition(pos)
    t.SetLayer(pcbnew.F_Fab)
    t.SetTextSize(pcbnew.VECTOR2I(pcbnew.FromMM(1.2), pcbnew.FromMM(1.2)))
    fp.Add(t)
    m = pcbnew.FP_3DMODEL()
    m.m_Filename = MODEL
    fp.Models().append(m)
    fp.Flip(pos, pcbnew.FLIP_DIRECTION_TOP_BOTTOM)
    return fp


if __name__ == "__main__":
    PCB = os.path.abspath(os.path.join(HERE, "..", "wallet.kicad_pcb"))
    b = pcbnew.LoadBoard(PCB)
    add(b, (100.0, 100.0))
    b.Save(PCB)
    print("BT1 added")
