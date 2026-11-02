"""
Point footprints at the project's STEP models (tools/gen_step_models.py), no copper changes:
  - J4 gets the display module on top of its socket
  - parts whose KiCad library model does not exist get our envelope models
BT1's model is set by add_battery.py.

Run on the routed board:  kicad python tools/models3d.py
"""
import os

import pcbnew

PCB = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "wallet.kicad_pcb"))
D = "${KIPRJMOD}/3d/"
LCD = D + "lcd_2in0_st7789_module.step"
BY_FOOTPRINT = {
    "USB_C_Receptacle_HRO_TYPE-C-31-M-12": D + "usb_c_hro_type_c_31_m_12.step",
    "JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal": D + "jst_ph_s2b_ph_sm4_tb.step",
    "SW_DPDT_CK_JS202011JCQN": D + "sw_slide_ck_js202011jcqn.step",
    "SW_Push_1P1T_XKB_TS-1187A": D + "sw_tact_xkb_ts1187a.step",
}


def _set(fp, files):
    models = fp.Models()
    if [m.m_Filename for m in models] == files:
        return False
    models.clear()
    for f in files:
        m = pcbnew.FP_3DMODEL()
        m.m_Filename = f
        models.append(m)
    return True


def attach(board):
    changed = []
    for fp in board.GetFootprints():
        ref, name = fp.GetReference(), str(fp.GetFPID().GetLibItemName())
        if ref == "J4":
            socket = [m.m_Filename for m in fp.Models() if "PinSocket" in m.m_Filename]
            files = socket + [LCD]
        elif name in BY_FOOTPRINT:
            files = [BY_FOOTPRINT[name]]
        else:
            continue
        if _set(fp, files):
            changed.append(ref)
    return changed


if __name__ == "__main__":
    b = pcbnew.LoadBoard(PCB)
    ch = attach(b)
    b.Save(PCB)
    print("models set on:", " ".join(ch) or "nothing (already current)")
