"""
Independent DRC/DFM on the manufacturing files (what the fab receives), in the spirit of
EAGLE/Fusion's DRC and JLCPCB's DFM check. It does not use KiCad's checker: it renders the
Gerbers itself (pygerber), reads the Excellon drill files and the IPC-D-356 netlist, and
checks the copper as pixels on a 10 um grid.

  connectivity   copper islands on every layer, joined by plated holes, compared with the
                 netlist: opens (= Fusion "air wires") and shorts, inner planes included
  clearance      copper of different nets closer than the limit, per layer
  width          copper narrower than the limit (tracks, necks, plane slivers)
  annular ring   PTH pads and vias, outer and inner layers
  hole spacing   PTH-PTH, other holes
  hole-copper    hole edge to copper of another net (outer / inner, via / PTH / NPTH)
  board edge     copper too close to the routed outline
  solder mask    mask webs (bridges) between openings narrower than the limit
  silkscreen     strokes thinner than the limit, silkscreen printed over exposed copper

Limits are JLCPCB's multilayer values (jlcpcb.com/capabilities/pcb-capabilities, read
2026-11-01). Pixel checks have about +-10 um (1 pixel) of resolution: results within
that of a limit are reported as "marginal", not as failures.

  python tools/gerber_drc.py [fab/gerbers]      (plain python: numpy, scipy, pillow, pygerber)
Writes fab/gerber_drc.md and fab/gerber_drc.png (violations marked). Exit 1 on any error.
"""
import math
import os
import re
import subprocess
import sys
import tempfile
from collections import defaultdict

import numpy as np
from PIL import Image
from scipy import ndimage as ndi

Image.MAX_IMAGE_PIXELS = None
HW = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
D = 100                       # pixels per mm (10 um)
PX = 1.0 / D
TOL = 1.5 * PX              # pixel quantisation: within this of a limit = marginal, not a failure

RULES = {                     # mm, JLCPCB multilayer
    "clearance": 0.09, "width": 0.09, "edge": 0.2,
    "ring_pth": 0.15, "ring_pth_recommended": 0.20, "ring_via": 0.05,
    "hole_pth_pth": 0.45, "hole_other": 0.2,
    "via_hole_copper": 0.2, "pth_hole_track": 0.28, "npth_hole_copper": 0.2,
    "inner_via_hole_copper": 0.2, "inner_pth_hole_copper": 0.3,
    "mask_web": 0.1, "silk_width": 0.15,
}
LAYERS = ["F_Cu", "In1_Cu", "In2_Cu", "B_Cu"]


# ---------------------------------------------------------------- inputs
def find(gdir, part):
    for f in os.listdir(gdir):
        if part in f:
            return os.path.join(gdir, f)
    return None


def render(path):
    """Gerber -> (bool image, min_x, max_y) in Gerber coordinates (mm, y up)."""
    from pygerber.common.rgba import RGBA
    from pygerber.gerberx3.api.v2 import ColorScheme, GerberFile, PixelFormatEnum
    w, k = RGBA.from_rgba(255, 255, 255, 255), RGBA.from_rgba(0, 0, 0, 255)
    cs = ColorScheme(background_color=k, clear_color=k, solid_color=w, clear_region_color=k,
                     solid_region_color=w, debug_1_color=w, debug_2_color=w)
    pf = GerberFile.from_file(path).parse()
    info = pf.get_info()
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "l.png")
        pf.render_raster(out, color_scheme=cs, dpmm=D, pixel_format=PixelFormatEnum.RGB)
        img = np.array(Image.open(out).convert("L")) > 127
    return img, float(info.min_x_mm), float(info.max_y_mm)


class Grid:
    def __init__(self, x0, y_top, w_mm, h_mm):
        self.x0, self.yt = x0, y_top
        self.h, self.w = int(math.ceil(h_mm * D)), int(math.ceil(w_mm * D))

    def place(self, img, min_x, max_y):
        out = np.zeros((self.h, self.w), bool)
        r0, c0 = int(round((self.yt - max_y) * D)), int(round((min_x - self.x0) * D))
        h, w = min(img.shape[0], self.h - r0), min(img.shape[1], self.w - c0)
        out[r0:r0 + h, c0:c0 + w] = img[:h, :w]
        return out

    def rc(self, x, y):
        return int(round((self.yt - y) * D)), int(round((x - self.x0) * D))

    def xy(self, r, c):
        return self.x0 + c * PX, self.yt - r * PX


def board_xy(x, y):
    """Gerber mm -> KiCad board coordinates (origin at the board corner (100, 100), y down)."""
    return round(x - 100, 2), round(-y - 100, 2)


def read_drills(gdir):
    holes = []                # (x, y, diameter, kind, slot_end or None)
    for name, plated in (("PTH.drl", True), ("NPTH.drl", False)):
        path = find(gdir, name)
        if not path or (plated and "NPTH" in path):
            path = next((os.path.join(gdir, f) for f in os.listdir(gdir)
                         if f.endswith(name) and (plated == ("NPTH" not in f))), None)
        if not path:
            continue
        tools, kinds, cur, func = {}, {}, None, None
        for line in open(path, encoding="utf8", errors="replace"):
            line = line.strip()
            m = re.match(r"; #@! TA\.AperFunction,(\w+),\w+,(\w+)", line)
            if m:
                func = m.group(2)
                continue
            m = re.match(r"^T(\d+)C([\d.]+)", line)
            if m:
                tools[m.group(1)] = float(m.group(2))
                kinds[m.group(1)] = ("via" if func == "ViaDrill" else "pth") if plated else "npth"
                continue
            m = re.match(r"^T(\d+)$", line)
            if m:
                cur = m.group(1)
                continue
            m = re.match(r"^X([-\d.]+)Y([-\d.]+)(?:G85X([-\d.]+)Y([-\d.]+))?$", line)
            if m and cur:
                end = (float(m.group(3)), float(m.group(4))) if m.group(3) else None
                holes.append((float(m.group(1)), float(m.group(2)), tools[cur], kinds[cur], end))
    return holes


def read_ipc(path):
    names, pts = {}, []       # pts: (net, ref, pin, x, y, access, is_via)
    for line in open(path, encoding="utf8", errors="replace"):
        m = re.match(r"^P  NNAME(\S+)\s+(.+?)\s*$", line)
        if m:
            names["NNAME" + m.group(1)] = m.group(2)
            continue
        if line[:3] in ("317", "327"):
            net = line[3:17].strip()
            ref, pin = line[20:26].strip(), line[27:31].strip()
            m = re.search(r"A(\d\d)X([+-]\d+)Y([+-]\d+)", line)
            if not m:
                continue
            x, y = int(m.group(2)) * 0.00254, int(m.group(3)) * 0.00254   # 0.0001 in -> mm
            pts.append((names.get(net, net), ref, pin, x, y, m.group(1), ref == "VIA"))
    return pts


# ---------------------------------------------------------------- helpers
def disk(r):
    r = max(int(r), 1)
    y, x = np.ogrid[-r:r + 1, -r:r + 1]
    return x * x + y * y <= r * r


class UF:
    def __init__(self):
        self.p = {}

    def f(self, a):
        self.p.setdefault(a, a)
        while self.p[a] != a:
            self.p[a] = self.p[self.p[a]]
            a = self.p[a]
        return a

    def u(self, a, b):
        self.p[self.f(a)] = self.f(b)


def ring_labels(lab, r, c, rad_px, width_px=3):
    """Labels of copper touching a hole wall: an annulus just outside the hole."""
    r0, r1, c0, c1 = max(r - rad_px - width_px, 0), r + rad_px + width_px + 1, max(c - rad_px - width_px, 0), c + rad_px + width_px + 1
    win = lab[r0:r1, c0:c1]
    yy, xx = np.ogrid[r0 - r:r1 - r, c0 - c:c1 - c]
    d2 = yy * yy + xx * xx
    ann = (d2 >= rad_px * rad_px) & (d2 <= (rad_px + width_px) ** 2)
    v = win[ann[:win.shape[0], :win.shape[1]]]
    return set(int(x) for x in np.unique(v) if x)


# ---------------------------------------------------------------- the check
def run(gdir, ipc_path, out_md, out_png):
    issues = []               # (severity, kind, (x, y) gerber mm, text)

    def add(sev, kind, x, y, text):
        issues.append((sev, kind, (x, y), text))

    edge_img, ex, ey = render(find(gdir, "Edge_Cuts"))
    ew, eh = edge_img.shape[1] * PX, edge_img.shape[0] * PX
    g = Grid(ex - 1.0, ey + 1.0, ew + 2.0, eh + 2.0)
    edge = g.place(edge_img, ex, ey)
    outside = ndi.label(~edge)[0]
    outside = outside == outside[0, 0]                    # flood fill from the corner
    board = ~outside & ~edge
    print("edge: board area %.1f mm2" % (board.sum() * PX * PX))

    cu = {L: g.place(*render(find(gdir, L))) for L in LAYERS}
    print("copper layers rendered:", ", ".join(LAYERS))
    holes = read_drills(gdir)
    pts = read_ipc(ipc_path)
    print(f"{len(holes)} holes, {len(pts)} netlist points")

    # -------- connectivity
    lab, nlab = {}, {}
    for L in LAYERS:
        lab[L], nlab[L] = ndi.label(cu[L], structure=np.ones((3, 3), bool))
    uf = UF()
    hole_nodes = []
    for (x, y, d, kind, end) in holes:
        if kind == "npth":
            hole_nodes.append(set())
            continue
        r, c = g.rc(x, y)
        nodes = set()
        for L in LAYERS:
            for l in ring_labels(lab[L], r, c, int(d / 2 * D) + 1):
                nodes.add((L, l))
        nodes = list(nodes)
        for a in nodes[1:]:
            uf.u(nodes[0], a)
        hole_nodes.append(set(nodes))

    def node_at(x, y, access):
        r, c = g.rc(x, y)
        layers = {"01": ["F_Cu"], "02": ["B_Cu"]}.get(access, LAYERS)
        for L in layers:
            win = lab[L][max(r - 3, 0):r + 4, max(c - 3, 0):c + 4]
            v = win[win > 0]
            if v.size:
                return (L, int(np.bincount(v).argmax()))
        return None

    net_groups, group_nets = defaultdict(set), defaultdict(set)
    missing = []
    for net, ref, pin, x, y, acc, is_via in pts:
        n = node_at(x, y, acc)
        if n is None:
            missing.append((net, ref, pin, x, y))
            continue
        if net.startswith("N/C") or net == "":
            continue
        grp = uf.f(n)
        net_groups[net].add(grp)
        group_nets[grp].add(net)
    for net, ref, pin, x, y in missing:
        add("error", "no copper at pad", x, y, f"{ref}.{pin} ({net}): netlist point has no copper under it")
    opens = 0
    for net, grps in sorted(net_groups.items()):
        if len(grps) > 1 and not net.startswith("unconnected-"):
            opens += len(grps) - 1
            locs = []
            for grp in grps:
                p = next((q for q in pts if q[0] == net and node_at(q[3], q[4], q[5]) and uf.f(node_at(q[3], q[4], q[5])) == grp), None)
                if p:
                    locs.append(f"{p[1]}.{p[2]}")
            x, y = next((q[3], q[4]) for q in pts if q[0] == net)
            add("error", "open (air wire)", x, y, f"net {net} is in {len(grps)} separate pieces: {', '.join(locs)}")
    shorts = 0
    for grp, nets in group_nets.items():
        real = sorted(n for n in nets if not n.startswith("unconnected-"))
        if len(real) > 1 or (len(nets) > 1 and any(n.startswith("unconnected-") for n in nets)):
            shorts += 1
            x, y = next((q[3], q[4]) for q in pts if q[0] in nets)
            add("error", "short", x, y, "nets connected together: " + ", ".join(sorted(nets)))
    print(f"connectivity: {opens} opens, {shorts} shorts")

    # net id per (layer, label); copper not reached by the netlist gets its own id
    netid, ids = {}, {}
    def nid(name):
        return ids.setdefault(name, len(ids) + 1)
    label_net = {}
    for L in LAYERS:
        m = np.zeros(nlab[L] + 1, np.int32)
        for l in range(1, nlab[L] + 1):
            nets = group_nets.get(uf.f((L, l)))
            m[l] = nid(sorted(nets)[0]) if nets else nid(f"floating {L} {l}")
        label_net[L] = m
        netid[L] = m[lab[L]]

    # -------- clearance (different nets) and width, per layer
    rc_px = RULES["clearance"] / 2 * D
    for L in LAYERS:
        img = netid[L]
        fp = disk(math.floor(rc_px - 0.5))               # 4 px: catches gaps <= ~0.08-0.09 mm
        mx = ndi.maximum_filter(img, footprint=fp)
        mn = ndi.minimum_filter(np.where(img == 0, np.iinfo(np.int32).max, img), footprint=fp)
        viol = (mx > 0) & (mn < np.iinfo(np.int32).max) & (mx != mn)
        for sl, n in blobs(viol):
            r, c = center(sl)
            x, y = g.xy(r, c)
            add("error", "clearance", x, y, f"{L}: copper of different nets closer than {RULES['clearance']} mm")
        # width: what an opening with a disk of the minimum width removes, far from the kept copper
        rw = int(round(RULES["width"] / 2 * D)) - 1
        opened = ndi.binary_opening(cu[L], structure=disk(rw))
        resid = cu[L] & ~opened
        far = ndi.distance_transform_edt(~opened) > rw + 1
        thin = resid & far
        for sl, n in blobs(thin, min_px=20):
            r, c = center(sl)
            x, y = g.xy(r, c)
            add("error", "min width", x, y, f"{L}: copper narrower than {RULES['width']} mm ({n} px)")

    # -------- board edge
    # distance to the outline's centre line: the outside starts half a line width (0.05 mm) out
    dist_edge = ndi.distance_transform_edt(~outside) * PX - 0.05
    for L in LAYERS:
        close = cu[L] & ~outside & (dist_edge < RULES["edge"] - TOL)
        for sl, n in blobs(close, min_px=4):
            r, c = center(sl)
            x, y = g.xy(r, c)
            add("error", "copper to edge", x, y, f"{L}: copper within {RULES['edge']} mm of the board edge")
        stray = cu[L] & outside
        if stray.any():
            r, c = center(blobs(stray)[0][0])
            x, y = g.xy(r, c)
            add("error", "copper outside board", x, y, f"{L}: copper outside the board outline")

    # -------- holes: ring, spacing, hole-to-copper
    for i, (x, y, d, kind, end) in enumerate(holes):
        r, c = g.rc(x, y)
        rad = d / 2
        if kind != "npth" and end is None:
            for L in ("F_Cu", "B_Cu") + (("In1_Cu", "In2_Cu") if kind == "via" else ()):
                ring = ring_width(cu[L], r, c, rad * D)
                if ring is None:
                    continue
                need = RULES["ring_via"] if kind == "via" else RULES["ring_pth"]
                if ring < need - PX:
                    add("error", "annular ring", x, y, f"{kind} {d} mm on {L}: ring {ring:.3f} mm < {need} mm")
                elif kind == "pth" and L in ("F_Cu", "B_Cu") and ring < RULES["ring_pth_recommended"] - PX:
                    add("warning", "annular ring", x, y, f"PTH {d} mm on {L}: ring {ring:.3f} mm (JLCPCB recommends {RULES['ring_pth_recommended']})")
        # hole to copper of other nets
        hole_nets = {int(label_net[L][l]) for (L, l) in hole_nodes[i]}
        for L in LAYERS:
            inner = L.startswith("In")
            lim = {"via": RULES["inner_via_hole_copper"] if inner else RULES["via_hole_copper"],
                   "pth": RULES["inner_pth_hole_copper"] if inner else RULES["pth_hole_track"],
                   "npth": RULES["npth_hole_copper"]}[kind]
            R = int(math.ceil((rad + lim) * D)) + 2
            r0, c0 = max(r - R, 0), max(c - R, 0)
            win = netid[L][r0:r + R + 1, c0:c + R + 1]
            yy, xx = np.ogrid[r0 - r:r0 - r + win.shape[0], c0 - c:c0 - c + win.shape[1]]
            foreign = (win > 0) if kind == "npth" else ((win > 0) & ~np.isin(win, list(hole_nets)))
            if not foreign.any():
                continue
            gap = float(np.sqrt((yy * yy + xx * xx)[foreign]).min()) * PX - rad - PX / 2
            if gap < lim - TOL:
                add("error", "hole to copper", x, y, f"{kind} {d} mm on {L}: copper of another net {gap:.3f} mm from the hole (< {lim})")
            elif gap < lim:
                add("warning", "hole to copper (marginal)", x, y, f"{kind} {d} mm on {L}: {gap:.3f} mm, within 1 px of {lim}")
    pl = [(x, y, d, kind) for (x, y, d, kind, end) in holes]
    for i in range(len(pl)):
        for j in range(i + 1, len(pl)):
            (xa, ya, da, ka), (xb, yb, db, kb) = pl[i], pl[j]
            gap = math.hypot(xa - xb, ya - yb) - (da + db) / 2
            if gap <= 0:
                continue
            lim = RULES["hole_pth_pth"] if ka == kb == "pth" else RULES["hole_other"]
            if gap < lim:
                add("error", "hole spacing", xa, ya, f"{ka}-{kb} holes {gap:.3f} mm apart (< {lim} mm)")

    # -------- solder mask webs and silkscreen
    for side, mask_name, silk_name, cu_name in (("top", "F_Mask", "F_Silkscreen", "F_Cu"), ("bottom", "B_Mask", "B_Silkscreen", "B_Cu")):
        mpath, spath = find(gdir, mask_name), find(gdir, silk_name)
        if mpath:
            mask = g.place(*render(mpath))                 # openings
            rr = int(round(RULES["mask_web"] / 2 * D))
            closed = ndi.binary_closing(mask, structure=disk(rr))
            webs = closed & ~mask & board
            for sl, n in blobs(webs, min_px=30):
                r, c = center(sl)
                x, y = g.xy(r, c)
                add("warning", "solder mask web", x, y, f"{side}: mask web narrower than {RULES['mask_web']} mm (fab removes it)")
        if spath:
            # KiCad cuts the silkscreen off exposed pads with clear-polarity (%LPC%) pad shapes;
            # pygerber draws clear aperture *macros* (rounded pads) solid, so read the drawn
            # silkscreen only (before %LPC%) and compute the overlap with the openings here
            src = open(spath, encoding="utf8", errors="replace").read()
            raw = src.split("%LPC*%")[0].rstrip() + "\nM02*\n"
            with tempfile.TemporaryDirectory() as td:
                rp = os.path.join(td, "silk.gbr")
                open(rp, "w", encoding="utf8").write(raw)
                silk = g.place(*render(rp))
            rs = int(round(RULES["silk_width"] / 2 * D)) - 1
            opened = ndi.binary_opening(silk, structure=disk(rs))
            thin = silk & ~opened & (ndi.distance_transform_edt(~opened) > rs + 1)
            for sl, n in blobs(thin, min_px=40):
                r, c = center(sl)
                x, y = g.xy(r, c)
                add("warning", "silkscreen width", x, y, f"{side}: silkscreen line thinner than {RULES['silk_width']} mm")
            if mpath:
                over = silk & mask & cu[cu_name]
                for sl, n in blobs(over, min_px=40):
                    r, c = center(sl)
                    x, y = g.xy(r, c)
                    add("info", "silkscreen clipped at pad", x, y, f"{side}: silkscreen drawn over an exposed pad (removed in the export)")

    write_report(issues, out_md, opens, shorts, len(holes), len(pts))
    write_png(cu, board, issues, g, out_png)
    errs = sum(1 for s in issues if s[0] == "error")
    warns = sum(1 for s in issues if s[0] == "warning")
    infos = sum(1 for s in issues if s[0] == "info")
    print(f"gerber DRC: {errs} errors, {warns} warnings, {infos} notes -> {out_md}")
    return errs


def ring_width(img, r, c, rad_px):
    """Smallest copper ring around a hole on one layer (mm), None if no pad there."""
    widths = []
    for a in range(0, 360, 5):
        dx, dy = math.cos(math.radians(a)), math.sin(math.radians(a))
        k = rad_px
        rr, cc = int(round(r + dy * (k + 1))), int(round(c + dx * (k + 1)))
        if not (0 <= rr < img.shape[0] and 0 <= cc < img.shape[1]) or not img[rr, cc]:
            widths.append(0.0)
            continue
        while True:
            rr, cc = int(round(r + dy * k)), int(round(c + dx * k))
            if not (0 <= rr < img.shape[0] and 0 <= cc < img.shape[1]) or not img[rr, cc] or k > rad_px + 400:
                break
            k += 1
        widths.append((k - rad_px) * PX)
    if max(widths) == 0:
        return None
    return min(widths)


def blobs(mask, min_px=1):
    lab, n = ndi.label(mask, structure=np.ones((3, 3), bool))
    out = []
    for i, sl in enumerate(ndi.find_objects(lab), 1):
        cnt = int((lab[sl] == i).sum())
        if cnt >= min_px:
            out.append((sl, cnt))
    return out


def center(sl):
    return (sl[0].start + sl[0].stop) // 2, (sl[1].start + sl[1].stop) // 2


def write_report(issues, path, opens, shorts, nholes, npts):
    by = defaultdict(list)
    for sev, kind, (x, y), text in issues:
        by[(sev, kind)].append((board_xy(x, y), text))
    L = ["# Gerber DRC (independent of KiCad's DRC)", "",
         "Checked: the Gerber and drill files the fab receives, rendered at 10 um, plus the IPC-D-356 netlist.",
         "Limits: JLCPCB multilayer (jlcpcb.com/capabilities/pcb-capabilities, read 2026-11-01).",
         "Positions are board coordinates in mm (origin at the top-left board corner, as in KiCad).", "",
         f"- holes: {nholes}, netlist points: {npts}",
         f"- connectivity: **{opens} opens** (Fusion: air wires), **{shorts} shorts**",
         f"- errors: **{sum(1 for i in issues if i[0] == 'error')}**, warnings: {sum(1 for i in issues if i[0] == 'warning')}, notes: {sum(1 for i in issues if i[0] == 'info')}", ""]
    for (sev, kind), items in sorted(by.items()):
        L.append(f"## {sev}: {kind} ({len(items)})")
        L.append("")
        for (bx, by_), text in items[:40]:
            L.append(f"- ({bx}, {by_}) {text}")
        if len(items) > 40:
            L.append(f"- ... and {len(items) - 40} more")
        L.append("")
    if not issues:
        L.append("No errors and no warnings.")
    open(path, "w", encoding="utf8").write("\n".join(L) + "\n")


def write_png(cu, board, issues, g, path):
    s = 4                                                  # 1 output pixel = 40 um
    h, w = cu["F_Cu"].shape
    img = np.zeros((h // s, w // s, 3), np.uint8)
    img[board[::s, ::s]] = (25, 60, 25)
    img[cu["B_Cu"][::s, ::s]] = (40, 70, 160)
    img[cu["F_Cu"][::s, ::s]] = (200, 70, 50)
    im = Image.fromarray(img)
    from PIL import ImageDraw
    dr = ImageDraw.Draw(im)
    for sev, kind, (x, y), text in issues:
        r, c = g.rc(x, y)
        col = (255, 255, 0) if sev == "error" else (0, 255, 255)
        dr.ellipse([c // s - 12, r // s - 12, c // s + 12, r // s + 12], outline=col, width=3)
    im.save(path)


def main():
    gdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HW, "fab", "gerbers")
    with tempfile.TemporaryDirectory() as td:
        ipc = os.path.join(td, "w.ipc")
        subprocess.run([CLI, "pcb", "export", "ipcd356", "-o", ipc, os.path.join(HW, "wallet.kicad_pcb")],
                       check=True, capture_output=True)
        errs = run(gdir, ipc, os.path.join(HW, "fab", "gerber_drc.md"), os.path.join(HW, "fab", "gerber_drc.png"))
    sys.exit(1 if errs else 0)


if __name__ == "__main__":
    main()
