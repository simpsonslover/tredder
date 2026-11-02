"""
Export the KiCad project for Autodesk Fusion's electronics import into hardware/fusion_ecad/
(and tredder-revA-fusion-ecad.zip).

Why: KiCad 10 writes nets by name only, e.g. (net "GND") on pads, tracks, vias and zones,
and drops the numbered net table. Fusion's KiCad importer only understands the older
form (KiCad 8/9): a net table at the top, (net 3 "GND") on pads, (net 3) on tracks
and vias, (net 3) (net_name "GND") on zones. Given the new form it imports every pad
with no net, and reports ~170 "Different connections on <part> pin X and pad Y (NET / none)"
consistency errors. The schematic is already written in the KiCad 8 format.

The copy is checked by loading it back into KiCad and comparing every pad, track,
via and zone net with the original.

Run with KiCad's python:  & "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" tools/export_fusion_ecad.py
"""
import math
import os
import re
import shutil
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ET

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.abspath(os.path.join(HERE, ".."))
OUT = os.path.join(HW, "fusion_ecad")
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
KICAD9 = "20241229"     # board file version of KiCad 9, the last one with numbered nets
COPY = ["wallet.kicad_sch", "wallet.kicad_pro", "wallet.kicad_dru", "jlcpcb-4L-real.edru", "wallet.kicad_sym", "fp-lib-table", "sym-lib-table"]


def node_start(t, pos):
    """Index of the '(' that opens the s-expression enclosing pos."""
    depth = 0
    for i in range(pos - 1, -1, -1):
        if t[i] == ")":
            depth += 1
        elif t[i] == "(":
            if depth == 0:
                return i
            depth -= 1
    return 0


def downgrade(text):
    names = []
    for m in re.finditer(r'\(net "((?:[^"\\]|\\.)*)"\)', text):
        if m.group(1) not in names:
            names.append(m.group(1))
    names.sort(key=lambda n: (n.startswith("unconnected-"), n))
    code = {n: i + 1 for i, n in enumerate(names)}

    def repl(m):
        name = m.group(1)
        s = node_start(text, m.start())
        kind = re.match(r"\((\w+)", text[s:s + 40]).group(1)
        if kind == "pad":
            return f'(net {code[name]} "{name}")'
        if kind in ("segment", "via", "arc"):
            return f"(net {code[name]})"
        if kind == "zone":
            return f'(net {code[name]})\n\t\t(net_name "{name}")'
        raise ValueError(f"net {name!r} inside unexpected node '{kind}'")

    out = re.sub(r'\(net "((?:[^"\\]|\\.)*)"\)', repl, text)
    # numbered net table right after (setup ...), where KiCad 9 writes it
    s = out.index("\n\t(setup")
    depth, i = 0, s + 1
    while True:
        if out[i] == "(":
            depth += 1
        elif out[i] == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    table = '\n\t(net 0 "")' + "".join(f'\n\t(net {c} "{n}")' for n, c in code.items())
    out = out[:i + 1] + table + out[i + 1:]
    out = re.sub(r"\(version \d+\)", f"(version {KICAD9})", out, count=1)
    # KiCad 9 files say every zone uses the modern fill; without this line an older-version
    # file counts as "legacy zone fill strategy" and the fills get converted best-effort
    if "filled_areas_thickness" not in out:
        out = re.sub(r"(\(min_thickness [^)]*\))", r"\1 (filled_areas_thickness no)", out)
    return out, code


def unique_pin_names(text):
    """Fusion (EAGLE) needs unique pin names in a symbol, and drops every connection of a
    symbol that repeats one ("GND" x8 on the Pico, "NC" x5 on the OPTIGA...). Rename repeats
    the EAGLE way, NAME@1, NAME@2, ..., which Fusion still displays as NAME. Pins connect
    by number, so the netlist does not change."""
    edits = []
    for m in re.finditer(r'\(symbol "([^"]+)"', text):
        if node_start(text, m.start()) and not re.match(r"\((lib_symbols|kicad_symbol_lib)", text[node_start(text, m.start()):][:20]):
            continue                                    # only top-level library symbols
        depth, i = 0, m.start()
        while True:                                     # end of this symbol block
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        names = [(n.start(1), n.end(1), n.group(1))
                 for n in re.finditer(r'\(name "([^"]*)"', text[m.start():i])
                 if text[node_start(text, m.start() + n.start()):][:5] == "(pin "]
        count = {}
        for _, _, n in names:
            count[n] = count.get(n, 0) + 1
        seen = {}
        for s, e, n in names:
            if count[n] > 1:
                seen[n] = seen.get(n, 0) + 1
                edits.append((m.start() + s, m.start() + e, f"{n}@{seen[n]}"))
    for s, e, new in sorted(edits, reverse=True):
        text = text[:s] + new + text[e:]
    return text, len(edits)


def block_end(text, s):
    """Index of the ')' closing the s-expression that opens at s."""
    depth, i = 0, s
    while True:
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i
        i += 1


def plain_custom_pads(text):
    """Custom-shaped SMD pads (the Pico's pin 1 and GND pins: a rectangle plus a rounded lobe,
    drawn as copper primitives) reach Fusion as a pad plus loose copper shapes with no net,
    which Fusion reports as Overlap / Copper Clearance. Replace each with a standard
    rounded-rectangle pad covering the same outline (the shapes' bounding box, centred)."""
    out, n, pos = [], 0, 0
    for m in re.finditer(r'\(pad "([^"]*)" smd custom', text):
        if m.start() < pos:
            continue
        s, e = m.start(), block_end(text, m.start())
        blk = text[s:e + 1]
        xs, ys = [], []
        for x, y in re.findall(r"\(xy ([-\d.]+) ([-\d.]+)\)", blk):
            xs.append(float(x))
            ys.append(float(y))
        for cx, cy, ex, ey in re.findall(r"\(gr_circle\s*\(center ([-\d.]+) ([-\d.]+)\)\s*\(end ([-\d.]+) ([-\d.]+)\)", blk):
            r = math.hypot(float(ex) - float(cx), float(ey) - float(cy))
            xs += [float(cx) - r, float(cx) + r]
            ys += [float(cy) - r, float(cy) + r]
        if not xs or abs(max(xs) + min(xs)) > 0.01 or abs(max(ys) + min(ys)) > 0.01:
            continue                                         # not centred: leave it alone
        keep = [re.search(r"\(at [^)]*\)", blk).group(0),
                re.search(r"\(layers [^)]*\)", blk).group(0)]
        for k in (r'\(net "[^"]*"\)', r'\(uuid "[^"]*"\)'):
            mm_ = re.search(k, blk)
            if mm_:
                keep.append(mm_.group(0))
        w, h = max(xs) - min(xs), max(ys) - min(ys)
        new = (f'(pad "{m.group(1)}" smd roundrect {keep[0]} (size {w:.4g} {h:.4g}) {keep[1]} '
               f'(roundrect_rratio 0.25) ' + " ".join(keep[2:]) + ")")
        out.append(text[pos:s] + new)
        pos = e + 1
        n += 1
    out.append(text[pos:])
    return "".join(out), n


def fusion_geometry(src):
    """Text of src with the changes Fusion needs to see the same board KiCad sees.
    Copper is unchanged. KiCad's python finds the items (removing pads through it
    crashes), the edits are made on the file text. Returns (text, summary)."""
    board = pcbnew.LoadBoard(src)
    text = open(src, encoding="utf8").read()
    drop, add = set(), []
    n = {"paste_pads": 0, "npth": 0, "stubs": 0, "custom": 0}
    mm = lambda v: f"{pcbnew.ToMM(v):.6f}".rstrip("0").rstrip(".")
    for fp in board.GetFootprints():
        for p in fp.Pads():
            # 1. pads with no copper layers (the Pico footprint's 40 solder-paste openings)
            #    are imported by Fusion as copper SMD pads that overlap the GND pins
            if not p.GetLayerSet().CuStack():
                drop.add(p.m_Uuid.AsString())
                n["paste_pads"] += 1
            # 2. non-plated holes (mounting holes, USB-C pegs, Tag-Connect guides) arrive as
            #    plated pads with a 0 mm ring; a plain hole in Fusion is a board cut-out
            elif p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                drop.add(p.m_Uuid.AsString())
                c, r = p.GetPosition(), p.GetDrillSize().x // 2
                add.append(f"\t(gr_circle (center {mm(c.x)} {mm(c.y)}) (end {mm(c.x + r)} {mm(c.y)}) "
                           f"(stroke (width 0.05) (type solid)) (fill no) (layer \"Edge.Cuts\") "
                           f"(uuid \"{uuid.uuid4()}\"))")
                n["npth"] += 1
    # 3. KiCad joins a track to a via/pad wherever the copper overlaps; Fusion only at the
    #    via/pad centre. Add a stub from each off-centre end to the centre, inside copper
    #    that is already there
    vias = [t for t in board.GetTracks() if t.GetClass() == "PCB_VIA"]
    pads = [p for fp in board.GetFootprints() for p in fp.Pads() if p.GetLayerSet().CuStack()]
    for t in [t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK"]:
        for e in (t.GetStart(), t.GetEnd()):
            hit = next((v for v in vias if v.GetNetCode() == t.GetNetCode() and v.HitTest(e)), None) or \
                next((p for p in pads if p.GetNetCode() == t.GetNetCode() and p.IsOnLayer(t.GetLayer())
                      and p.HitTest(e)), None)
            if hit is None or hit.GetPosition() == e:
                continue
            c = hit.GetPosition()
            add.append(f"\t(segment (start {mm(e.x)} {mm(e.y)}) (end {mm(c.x)} {mm(c.y)}) "
                       f"(width {mm(min(t.GetWidth(), pcbnew.FromMM(0.2)))}) "
                       f"(layer \"{board.GetLayerName(t.GetLayer())}\") (net \"{t.GetNetname()}\") "
                       f"(uuid \"{uuid.uuid4()}\"))")
            n["stubs"] += 1
    text, n["custom"] = plain_custom_pads(text)
    for u in drop:                                          # delete each (pad ...) block by its uuid
        k = text.index(f'(uuid "{u}")')
        s = node_start(text, k)
        depth, i = 0, s
        while True:
            if text[i] == "(":
                depth += 1
            elif text[i] == ")":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        line0 = text.rfind("\n", 0, s)
        text = text[:line0] + text[i + 1:]
    end = text.rstrip().rfind(")")                           # before the board's closing paren
    text = text[:end] + "\n".join(add) + "\n" + text[end:]
    return text, n


def nets_of(board):
    pads = {(fp.GetReference(), p.GetNumber(), p.GetPosition().x, p.GetPosition().y): p.GetNetname()
            for fp in board.GetFootprints() for p in fp.Pads()}
    tracks = sorted((t.GetClass(), t.GetStart().x, t.GetStart().y, t.GetEnd().x, t.GetEnd().y, t.GetNetname())
                    for t in board.GetTracks())
    zones = sorted((z.GetNetname(), z.GetLayer()) for z in board.Zones())
    return pads, tracks, zones


def netlist(sch):
    """{net name: sorted (ref, pin)} from kicad-cli's netlist of sch."""
    xml = os.path.join(OUT, "_netlist.xml")
    subprocess.run([CLI, "sch", "export", "netlist", "--format", "kicadxml", "-o", xml, sch],
                   check=True, capture_output=True)
    nets = {n.get("name"): tuple(sorted((x.get("ref"), x.get("pin")) for x in n.iter("node")))
            for n in ET.parse(xml).getroot().iter("net")}
    os.remove(xml)
    return nets


def main():
    src = os.path.join(HW, "wallet.kicad_pcb")
    os.makedirs(OUT, exist_ok=True)       # empty it but keep the folder: KiCad may have it open
    for f in os.listdir(OUT):
        p = os.path.join(OUT, f)
        shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
    for f in COPY:
        shutil.copy(os.path.join(HW, f), OUT)
    for f in ("wallet.kicad_sch", "wallet.kicad_sym"):
        p = os.path.join(OUT, f)
        fixed, n = unique_pin_names(open(p, encoding="utf8").read())
        with open(p, "w", encoding="utf8", newline="\n") as fh:
            fh.write(fixed)
        print(f"{f}: {n} repeated pin names renamed NAME@n")
    shutil.copytree(os.path.join(HW, "wallet.pretty"), os.path.join(OUT, "wallet.pretty"))
    shutil.copytree(os.path.join(HW, "3d"), os.path.join(OUT, "3d"))

    # same connections, but no-connect nets are named after the pin ("unconnected-(U2-NC-Pad2)"),
    # so renamed pins rename those nets: carry the new names over to the board
    old, new = netlist(os.path.join(HW, "wallet.kicad_sch")), netlist(os.path.join(OUT, "wallet.kicad_sch"))
    by_nodes = {v: k for k, v in new.items()}
    if set(old.values()) != set(by_nodes):
        sys.exit("export_fusion_ecad: renaming pins changed the schematic's connections")
    rename = {k: by_nodes[v] for k, v in old.items() if by_nodes[v] != k}

    # geometry fixes, still in KiCad 10 format. Written as wallet.kicad_pcb next to the copied
    # wallet.kicad_pro, so the re-pour (holes became edge cut-outs) uses this project's rules
    dst = os.path.join(OUT, "wallet.kicad_pcb")
    text, geo = fusion_geometry(src)
    with open(dst, "w", encoding="utf8", newline="\n") as fh:
        fh.write(text)
    board = pcbnew.LoadBoard(dst)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(dst)
    a = nets_of(board)
    text = open(dst, encoding="utf8").read()
    for x, y in rename.items():
        text = text.replace(f'(net "{x}")', f'(net "{y}")')
    out, code = downgrade(text)
    with open(dst, "w", encoding="utf8", newline="\n") as fh:
        fh.write(out)

    b = nets_of(pcbnew.LoadBoard(dst))
    a = ({k: rename.get(v, v) for k, v in a[0].items()},
         sorted(x[:-1] + (rename.get(x[-1], x[-1]),) for x in a[1]),
         sorted((rename.get(n, n), l) for n, l in a[2]))
    for what, x, y in zip(("pad", "track/via", "zone"), a, b):
        if x != y:
            sys.exit(f"export_fusion_ecad: {what} nets differ after conversion")
    pads = {(r, n): net for (r, n, _, _), net in b[0].items() if net}
    sch = {(r, pin): name for name, nodes in new.items() for r, pin in nodes}
    bad = [k for k, v in pads.items() if sch.get(k) != v]
    if bad:
        sys.exit(f"export_fusion_ecad: board and schematic disagree on {len(bad)} pads, e.g. {bad[:3]}")
    rpt = os.path.join(OUT, "_drc.rpt")
    r = subprocess.run([CLI, "pcb", "drc", "--schematic-parity", "--severity-error", "-o", rpt, dst],
                       capture_output=True, text=True)
    log = r.stdout + r.stderr
    os.remove(rpt)
    if "Found 0 unconnected items" not in log or "Found 0 schematic parity issues" not in log:
        sys.exit("export_fusion_ecad: the Fusion copy fails KiCad's connection/parity check:\n" + log)
    print(f"geometry for Fusion: {geo['custom']} custom-shaped pads made standard, "
          f"{geo['paste_pads']} copper-less paste pads removed, {geo['npth']} plain holes "
          f"made board cut-outs, {geo['stubs']} track ends joined to their via/pad centre")
    print("KiCad DRC of the copy:", " / ".join(l for l in log.splitlines() if l.startswith("Found")))
    print(f"fusion_ecad: {len(code)} nets ({len(rename)} renamed), {len(pads)} connected pads, "
          f"{len(a[1])} tracks/vias, {len(a[2])} zones: identical when loaded back; "
          "every board pad has its schematic net")
    z = shutil.make_archive(os.path.join(HW, "mech", "tredder-revA-fusion-ecad"), "zip", HW, "fusion_ecad")
    print("wrote", os.path.relpath(z, HW))


if __name__ == "__main__":
    main()
