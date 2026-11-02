"""
Generate wallet.kicad_pcb from design.py (run with KiCad's python).

- 4-layer board: F.Cu signals / In1 GND / In2 +3V3 / B.Cu signals + GND.
- Every footprint is linked to its schematic symbol UUID, so in the KiCad GUI
  "Update PCB from Schematic" (F8) works normally after this.
- All parts on the top side (single-sided assembly). The display module sits
  above the board on J4; parts under it must stay < ~7 mm tall.
"""
import math
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(__file__))
import design as D  # noqa: E402
import gen_sch  # noqa: E402  (for symbol UUIDs)

HERE = os.path.dirname(os.path.abspath(__file__))
PRJ = os.path.abspath(os.path.join(HERE, ".."))
STOCK = r"C:/Program Files/KiCad/10.0/share/kicad/footprints"
mm = pcbnew.FromMM
OX, OY = 100.0, 100.0           # board origin on the page
BW, BH, BR = 85.6, 54.0, 3.0     # credit-card outline

# ref: (x, y, rot_deg)  -- board-relative mm, origin top-left, top view
PLACE = {
    # MCU: antenna end flush with right edge (Raspberry Pi guidance)
    "A1": (BW - 25.5, 15.5, 90),
    # Display header (module overhangs x 2..62, y 2..38 -- see F.Fab outline)
    "J4": (4.5, 6.0, 0),
    "C9": (8.5, 10.0, 90), "Q4": (9.0, 25.0, 0), "R25": (9.0, 21.5, 90),
    # Display power switch (sleep): next to J4 VCC, under the module
    "Q5": (13.5, 14.0, 0), "R30": (13.5, 10.5, 0), "R31": (17.5, 14.0, 90), "C17": (17.5, 10.0, 90),
    # Secure element: under the display, close to GP8/GP9, away from edges
    "U2": (62.5, 31.5, 0), "Q2": (69.5, 31.0, 90), "R12": (72.0, 31.0, 90),
    "R13": (56.8, 30.5, 90), "R14": (58.4, 30.5, 90), "C4": (62.5, 35.5, 0), "C5": (66.0, 35.5, 90),
    # USB-C bottom edge
    "J1": (40.0, BH - 4.0, 0), "R1": (36.0, 43.0, 90), "R2": (44.0, 43.0, 90),
    "U1": (40.0, 41.5, 0), "F1": (46.0, 40.0, 90), "R3": (36.0, 39.0, 0), "R4": (40.0, 38.5, 0),
    # Charger / power path
    "U3": (30.0, 33.0, 0), "C1": (26.5, 33.0, 90), "C2": (33.5, 33.0, 90), "R5": (30.0, 36.5, 0),
    "R6": (23.5, 29.0, 0), "D1": (23.5, 31.5, 0), "R7": (27.0, 29.0, 0), "R8": (30.5, 29.0, 0),
    "J2": (5.4, 31.3, 270), "SW1": (29.0, BH - 3.5, 0),
    "Q1": (36.0, 29.5, 0), "R9": (40.0, 29.5, 90), "C3": (40.0, 33.5, 90),
    "R10": (47.5, 29.5, 90), "R11": (49.1, 29.5, 90), "C16": (50.7, 29.5, 90),
    "C14": (44.0, 30.5, 90), "C15": (44.0, 34.5, 90),
    # microSD bottom-left corner, card inserts from the bottom edge
    "J3": (16.0, BH - 8.75, 0),
    "Q3": (16.0, 33.0, 0), "R21": (12.5, 33.0, 90), "R23": (19.5, 33.0, 90), "C8": (21.0, 33.0, 90),
    "C6": (12.4, 25.5, 90), "C7": (13.6, 29.0, 90),
    "R15": (15.2, 29.0, 90), "R16": (16.8, 29.0, 90), "R17": (18.4, 29.0, 90),
    "R18": (20.0, 29.0, 90), "R19": (20.0, 25.5, 90), "R20": (18.4, 25.5, 90),
    # Buttons: Back / Confirm well apart; Up / Down at the right
    "SW4": (53.0, BH - 6.0, 0), "SW5": (65.0, BH - 6.0, 0),
    "SW2": (76.0, 37.0, 0), "SW3": (76.0, BH - 6.0, 0),
    "R26": (75.0, 30.5, 90), "C10": (76.6, 30.5, 90), "R27": (78.2, 30.5, 90), "C11": (79.8, 30.5, 90),
    "R28": (52.0, 34.0, 90), "C12": (53.6, 34.0, 90), "R29": (55.2, 34.0, 90), "C13": (56.8, 34.0, 90),
    # Debug (prototype only)
    "J5": (52.0, 40.5, 0),
    "TP1": (58.0, 40.5, 0), "TP2": (60.5, 40.5, 0), "TP3": (63.0, 40.5, 0), "TP4": (65.5, 40.5, 0),
    "TP5": (68.0, 40.5, 0), "TP6": (70.5, 40.5, 0), "TP7": (58.0, 43.0, 0), "TP8": (60.5, 43.0, 0),
    # Mounting holes (M2, low-head screws: H1/H4 sit under the display module)
    "H1": (11.0, 3.2, 0), "H2": (3.2, BH - 3.2, 0), "H3": (BW - 3.0, BH - 3.0, 0), "H4": (31.5, 3.2, 0),
}


def P(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))


def load_fp(libid):
    lib, name = libid.split(":")
    path = os.path.join(PRJ, "wallet.pretty") if lib == "wallet" else os.path.join(STOCK, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        raise SystemExit(f"footprint not found: {libid}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def edge(board):
    def seg(a, b):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(P(*a)); s.SetEnd(P(*b)); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)

    def arc(c, a0):
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        pts = [(c[0] + BR * math.cos(math.radians(a)), c[1] + BR * math.sin(math.radians(a)))
               for a in (a0, a0 + 45, a0 + 90)]
        s.SetArcGeometry(P(*pts[0]), P(*pts[1]), P(*pts[2]))
        s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
        board.Add(s)

    seg((BR, 0), (BW - BR, 0)); seg((BW, BR), (BW, BH - BR))
    seg((BW - BR, BH), (BR, BH)); seg((0, BH - BR), (0, BR))
    arc((BW - BR, BR), 270); arc((BW - BR, BH - BR), 0); arc((BR, BH - BR), 90); arc((BR, BR), 180)


def zone(board, net, layer, prio=0, inset=0.3):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net)
    z.SetAssignedPriority(prio)
    z.SetLocalClearance(mm(0.25))
    z.SetMinThickness(mm(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    o = z.Outline()
    o.NewOutline()
    for x, y in [(inset, inset), (BW - inset, inset), (BW - inset, BH - inset), (inset, BH - inset)]:
        o.Append(mm(OX + x), mm(OY + y))
    board.Add(z)


SILK_MIN_H, SILK_MIN_W = 1.0, 0.15   # JLCPCB: silkscreen text >= 1.0 mm high, line >= 0.15 mm


def text(board, s, x, y, layer=pcbnew.F_SilkS, size=1.0, rot=0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s); t.SetPosition(P(x, y)); t.SetLayer(layer)
    w = size * 0.15
    if layer in (pcbnew.F_SilkS, pcbnew.B_SilkS):
        size, w = max(size, SILK_MIN_H), max(w, SILK_MIN_W)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size))); t.SetTextThickness(mm(w))
    t.SetTextAngleDegrees(rot)
    t.SetMirrored(layer == pcbnew.B_SilkS)
    board.Add(t)


def rect(board, x0, y0, x1, y1, layer, w=0.12):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_RECTANGLE)
    s.SetStart(P(x0, y0)); s.SetEnd(P(x1, y1)); s.SetLayer(layer); s.SetWidth(mm(w))
    board.Add(s)


def fanout(board, plane_nets, via_d=0.6, drill=0.3, width=0.3, clr=0.2):
    """Stub + via from every SMD pad on a plane net down to the inner planes.

    Tries the natural direction first (footprint centre -> pad, i.e. outward),
    then rotated directions and longer stubs, and keeps the first spot that
    clears other-net pads, earlier vias, the board edge and the bodies of the
    Pico module / connectors (no vias under the module's exposed test pads).
    """
    def r2mm(b):
        return (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()), pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))

    def dist_rect(x, y, r):
        dx = max(r[0] - x, 0, x - r[2]); dy = max(r[1] - y, 0, y - r[3])
        return math.hypot(dx, dy)

    pads = []   # (rect_mm, netname, pad)
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.IsOnLayer(pcbnew.F_Cu) or pad.IsOnLayer(pcbnew.B_Cu):  # skip paste-only pads
                pads.append((r2mm(pad.GetBoundingBox()), pad.GetNetname(), pad))
    keep_out = []
    for fp in board.GetFootprints():
        if fp.GetReference() in ("J1", "J3", "J5", "SW1"):
            keep_out.append(r2mm(fp.GetCourtyard(pcbnew.F_CrtYd).BBox()))
    ax, ay, _ = PLACE["A1"]  # Pico body 51 x 21 mm (rotated 90): no vias under it
    keep_out.append((OX + ax - 25.5, OY + ay - 10.5, OX + ax + 25.5, OY + ay + 10.5))
    vias = []  # (x, y, net)

    def ok(px, py, vx, vy, net, own):
        bx, by = vx - OX, vy - OY
        if not (0.8 < bx < BW - 0.8 and 0.8 < by < BH - 0.8):
            return False
        if any(dist_rect(vx, vy, k) < via_d / 2 + 0.1 for k in keep_out):
            return False
        for x, y, n in vias:
            if math.hypot(vx - x, vy - y) < via_d + (0.25 if n == net else clr + 0.05):
                return False
        steps = 8
        for r, n, pad in pads:
            if pad is own:
                continue
            same = n == net
            if dist_rect(vx, vy, r) < via_d / 2 + (0.05 if same else clr):
                return False
            if same:
                continue
            for k in range(1, steps + 1):
                tx = px + (vx - px) * k / steps; ty = py + (vy - py) * k / steps
                if dist_rect(tx, ty, r) < width / 2 + clr:
                    return False
        return True

    n = 0
    fails = []
    for fp in board.GetFootprints():
        c = fp.GetPosition()
        for pad in fp.Pads():
            net = pad.GetNetname()
            if net not in plane_nets or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
                continue
            p = pad.GetPosition()
            px, py = pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)
            dx, dy = p.x - c.x, p.y - c.y
            L = math.hypot(dx, dy)
            base = math.atan2(dy, dx) if L > mm(0.05) else -math.pi / 2
            bb = pad.GetBoundingBox()
            hw, hh = pcbnew.ToMM(bb.GetWidth()) / 2, pcbnew.ToMM(bb.GetHeight()) / 2
            spot = None
            for extra in (0.35, 0.6, 0.9, 1.3, 1.8):
                for rot in (0, 45, -45, 90, -90, 135, -135, 180):
                    a = base + math.radians(rot)
                    ux, uy = math.cos(a), math.sin(a)
                    reach = abs(ux) * hw + abs(uy) * hh
                    d = reach + extra + via_d / 2
                    vx, vy = px + ux * d, py + uy * d
                    if ok(px, py, vx, vy, net, pad):
                        spot = (vx, vy)
                        break
                if spot:
                    break
            if not spot:
                fails.append(f"{fp.GetReference()}.{pad.GetNumber()}")
                continue
            vias.append((spot[0], spot[1], net))
            vp = pcbnew.VECTOR2I(mm(spot[0]), mm(spot[1]))
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(vp); via.SetWidth(mm(via_d)); via.SetDrill(mm(drill))
            via.SetNet(pad.GetNet())
            board.Add(via)
            tr = pcbnew.PCB_TRACK(board)
            tr.SetStart(p); tr.SetEnd(vp); tr.SetWidth(mm(width)); tr.SetLayer(pcbnew.F_Cu)
            tr.SetNet(pad.GetNet())
            board.Add(tr)
            n += 1
    if fails:
        print("fanout: no room for", ", ".join(fails), "(left to the router)")
    return n


def build(out):
    pin_net = D.check()
    missing = set(D.P) - set(PLACE)
    assert not missing, f"no placement for {sorted(missing)}"

    board = pcbnew.NewBoard(out)
    board.SetCopperLayerCount(4)
    ds = board.GetDesignSettings()
    ds.m_TrackMinWidth = mm(0.127)
    ds.m_MinClearance = mm(0.127)
    ds.m_ViasMinSize = mm(0.45)
    ds.m_ViasMinAnnularWidth = mm(0.15)   # JLCPCB rule set: via ring >= 0.15 mm
    ds.m_MinThroughDrill = mm(0.2)
    ds.m_CopperEdgeClearance = mm(0.3)
    ds.m_HoleToHoleMin = mm(0.25)

    nets = {}
    for name in D.NETS:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n

    for ref, p in D.P.items():
        fp = load_fp(p["footprint"])
        fp.SetReference(ref)
        fp.SetValue(p["value"])
        fp.SetPath(pcbnew.KIID_PATH("/" + gen_sch.uid("sym", ref)))
        fp.SetSheetname("/")
        fp.SetSheetfile("wallet.kicad_sch")
        fp.SetField("Description", p["note"])
        for k in ("MPN", "LCSC"):
            fp.SetField(k, p[k.lower()])
            fld = fp.GetField(k)
            fld.SetVisible(False)
            fld.SetLayer(pcbnew.F_Fab)
        if p["dnp"]:
            fp.SetDNP(True)
            fp.SetExcludedFromBOM(True)
            fp.SetExcludedFromPosFiles(True)
        x, y, rot = PLACE[ref]
        fp.SetPosition(P(x, y))
        fp.SetOrientationDegrees(rot)
        pads = {}
        for pad in fp.Pads():
            pads.setdefault(pad.GetNumber(), []).append(pad)
        t = D.TYPES[p["type"]]
        for num, _ in t["left"] + t["right"]:
            if num not in pads:
                raise SystemExit(f"{ref}: symbol pin {num} has no pad in {p['footprint']}")
            rp = f"{ref}.{num}"
            if rp in pin_net:
                net = nets[pin_net[rp]]
            else:  # no-connect: use the net name eeschema generates, keeps parity clean
                nm = f"unconnected-({ref}-{dict(t['left'] + t['right'])[num]}-Pad{num})"
                net = nets.get(nm) or nets.setdefault(nm, pcbnew.NETINFO_ITEM(board, nm))
                board.Add(net)
            for pad in pads[num]:
                pad.SetNet(net)
        board.Add(fp)

    import models3d  # STEP models: display module on J4 + parts the KiCad library has no model for
    models3d.attach(board)
    import add_battery  # BT1: LiPo cell behind the board (board-only mechanical part + 3D model)
    add_battery.add(board, (OX, OY))

    edge(board)

    gnd, v33 = nets["GND"], nets["+3V3"]
    n_vias = fanout(board, {"GND", "+3V3"})
    # Tag-Connect power pads: its courtyard is a via keep-out (pogo legs), so drop straight down out of it
    jx, jy, _ = PLACE["J5"]
    for num, dx in (("1", -1.27), ("5", 1.27)):
        pad = [p for p in board.FindFootprintByReference("J5").Pads() if p.GetNumber() == num][0]
        vp = P(jx + dx, jy + 3.0)
        via = pcbnew.PCB_VIA(board)
        via.SetPosition(vp); via.SetWidth(mm(0.6)); via.SetDrill(mm(0.3)); via.SetNet(pad.GetNet())
        board.Add(via)
        tr = pcbnew.PCB_TRACK(board)
        tr.SetStart(pad.GetPosition()); tr.SetEnd(vp); tr.SetWidth(mm(0.3)); tr.SetLayer(pcbnew.F_Cu)
        tr.SetNet(pad.GetNet())
        board.Add(tr)
        n_vias += 1
    zone(board, gnd, pcbnew.In1_Cu)
    zone(board, v33, pcbnew.In2_Cu)
    zone(board, gnd, pcbnew.B_Cu)
    zone(board, gnd, pcbnew.F_Cu, prio=0)

    # Display module outline (verify against your module's drawing!)
    rect(board, 2.0, 2.0, 62.0, 38.0, pcbnew.F_Fab)
    text(board, "LCD MODULE ABOVE (2.0in, verify outline)", 32, 20, pcbnew.F_Fab, 1.2)

    text(board, "WALLET rev A", 72.0, 26.8, pcbnew.F_Fab, 1.0)
    text(board, "UP", 76.0, 33.2, size=0.9); text(board, "DOWN", 76.0, BH - 1.6, size=0.9)
    text(board, "BACK", 53.0, BH - 1.6, size=0.9); text(board, "CONFIRM", 65.0, BH - 1.6, size=0.9)
    text(board, "PWR  ON>", 29.0, BH - 6.6, size=0.8)
    text(board, "BAT+ = pin 1", 1.6, 31.3, size=0.8, rot=90)
    text(board, "DEBUG - cover for production", 66.0, 43.2, pcbnew.F_SilkS, 0.8)
    text(board, "SWD/UART disabled in OTP for production. Seed never stored in plain form.",
         OX * 0 + 42.8, 27.0, pcbnew.B_SilkS, 0.8, 0)

    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(out)
    print(f"pcb: {len(D.P)} footprints, {len(nets)} nets, {n_vias} plane vias -> {out}")


if __name__ == "__main__":
    build(os.path.join(PRJ, "wallet.kicad_pcb"))
