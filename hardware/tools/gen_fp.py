"""
Build project footprints in hardware/wallet.pretty (run with KiCad's python).

1. RaspberryPi_Pico_2_W_SMD_SolderHoles
   Stock Pico W SMD footprint, but the underside pads we need (USB D+/D-/GND
   test points and the 3 SWD pads) get 0.8 mm plated holes, so the module can
   be soldered by hand: solder the castellations from the top, then feed
   solder through each hole from the bottom of the carrier.
2. Infineon_PG-USON-10-2_3x3mm_P0.5mm
   OPTIGA Trust M package. Derived from the KiCad USON-10 3x3 P0.5 land
   pattern with the exposed pad removed (datasheet Table 6 lists no EP).
   >>> Verify the land pattern against Figure 6 of the OPTIGA datasheet. <<<
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


def uson():
    fp = pcbnew.FootprintLoad(os.path.join(STOCK, "Package_SON.pretty"),
                              "Microchip_USON-10-1EP_3x3mm_P0.5mm_EP1.8x2.5mm")
    for pad in list(fp.Pads()):
        if pad.GetNumber() in ("11", ""):
            fp.Remove(pad)
    save(fp, "Infineon_PG-USON-10-2_3x3mm_P0.5mm",
         "Infineon PG-USON-10-2/-4 (OPTIGA Trust M), 3x3 mm, 0.5 mm pitch, no exposed pad. "
         "VERIFY against OPTIGA Trust M datasheet Figure 6 before ordering")


if __name__ == "__main__":
    pico()
    uson()
