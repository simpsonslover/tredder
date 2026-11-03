"""
Build project footprints in hardware/wallet.pretty (run with KiCad's python).

1. RaspberryPi_Pico_2_W_SMD_SolderHoles
   Stock Pico W SMD footprint, but the underside pads we need (USB D+/D-/GND
   test points and the 3 SWD pads) get 0.8 mm plated holes, so the module can
   be soldered by hand: solder the castellations from the top, then feed
   solder through each hole from the bottom of the carrier.
2. NXP_HX2QFN-20-1EP_3x3mm_P0.4mm_EP1.7x1.7mm
   NXP SE050 package (HX2QFN20, SOT1969-1). KiCad's WQFN-20 3x3 P0.4 land
   pattern (0.2 x 0.825 mm pads, 1.7 x 1.7 mm centre pad), which matches the
   EasyEDA land pattern LCSC publishes for the SE050C2 (C2659345). Pin
   numbering follows SE050 datasheet Figure 4 (pin 1 top-left, counter-
   clockwise). The centre pad is pad 21 (GND).
"""
import os

import pcbnew

STOCK = r"C:/Program Files/KiCad/10.0/share/kicad/footprints"
OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "wallet.pretty"))
mm = pcbnew.FromMM
IO = pcbnew.PCB_IO_MGR.FindPlugin(pcbnew.PCB_IO_MGR.KICAD_SEXP)


def save(fp, name, descr):
    fp.SetFPID(pcbnew.LIB_ID("wallet", name))
    fp.SetLibDescription(descr)
    os.makedirs(OUT, exist_ok=True)
    IO.FootprintSave(OUT, fp)
    print("saved", name)


def pico():
    fp = pcbnew.FootprintLoad(os.path.join(STOCK, "Module.pretty"), "RaspberryPi_Pico_W_SMD")
    for pad in fp.Pads():
        if pad.GetNumber() in ("D1", "D2", "D3", "TP1", "TP2", "TP3"):
            pad.SetAttribute(pcbnew.PAD_ATTRIB_PTH)
            pad.SetDrillShape(pcbnew.PAD_DRILL_SHAPE_CIRCLE)
            pad.SetDrillSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
            pad.SetLayerSet(pcbnew.PAD.PTHMask())
    save(fp, "RaspberryPi_Pico_2_W_SMD_SolderHoles",
         "Raspberry Pi Pico 2 W, castellated SMD, with plated solder-access holes under "
         "TP1-TP3 (USB) and D1-D3 (SWD) so the underside pads can be hand-soldered from the back")


def hx2qfn():
    fp = pcbnew.FootprintLoad(os.path.join(STOCK, "Package_DFN_QFN.pretty"),
                              "WQFN-20-1EP_3x3mm_P0.4mm_EP1.7x1.7mm")
    # drop the stock footprint's separate paste openings (Fusion's import rejects copper-less
    # pads) and give the centre pad its own, reduced (about 60 % area) paste instead
    pads = list(fp.Pads())          # (removing pads breaks iterating fp.Pads() afterwards)
    ep = next(p for p in pads if p.GetNumber() == "21")
    for pad in pads:
        if pad.GetNumber() == "":
            fp.Remove(pad)
    ep.SetLayerSet(pcbnew.LSET(ep.GetLayerSet()).AddLayer(pcbnew.F_Paste))
    ep.SetLocalSolderPasteMarginRatio(-0.2)
    # GND pins join through tracks (pin 1 to the centre pad on the board), not through the copper
    # pour: at 0.4 mm pitch a pour could only reach them with a single thermal spoke
    fp.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_NONE)
    for g in fp.GraphicalItems():   # JLCPCB minimum silkscreen line width
        if g.GetLayer() == pcbnew.F_SilkS and g.GetWidth() < pcbnew.FromMM(0.15):
            g.SetWidth(pcbnew.FromMM(0.15))
    fp.Models().clear()             # KiCad has no HX2QFN-20 model: models3d.py attaches ours
    save(fp, "NXP_HX2QFN-20-1EP_3x3mm_P0.4mm_EP1.7x1.7mm",
         "NXP HX2QFN20 (SOT1969-1, SE050), 3x3x0.32 mm, 0.4 mm pitch, centre pad 1.7 x 1.7 mm "
         "(pad 21, GND), per SE050 datasheet rev 3.7 Figure 4")


if __name__ == "__main__":
    pico()
    hx2qfn()
