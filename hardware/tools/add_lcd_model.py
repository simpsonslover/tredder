"""Attach the placeholder display 3D model to J4 in wallet.kicad_pcb (no copper changes)."""
import os

import pcbnew

PCB = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "wallet.kicad_pcb"))
MODEL = "${KIPRJMOD}/3d/lcd_2in0_st7789_module.wrl"


def attach(fp):
    if any(m.m_Filename == MODEL for m in fp.Models()):
        return False
    m = pcbnew.FP_3DMODEL()
    m.m_Filename = MODEL
    fp.Models().append(m)
    return True


if __name__ == "__main__":
    b = pcbnew.LoadBoard(PCB)
    print("added" if attach(b.FindFootprintByReference("J4")) else "already present")
    b.Save(PCB)
