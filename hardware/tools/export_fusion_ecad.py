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
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.abspath(os.path.join(HERE, ".."))
OUT = os.path.join(HW, "fusion_ecad")
CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
KICAD9 = "20241229"     # board file version of KiCad 9, the last one with numbered nets
COPY = ["wallet.kicad_sch", "wallet.kicad_pro", "wallet.kicad_sym", "fp-lib-table", "sym-lib-table"]


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
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
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

    text = open(src, encoding="utf8").read()
    for a, b in rename.items():
        text = text.replace(f'(net "{a}")', f'(net "{b}")')
    out, code = downgrade(text)
    dst = os.path.join(OUT, "wallet.kicad_pcb")
    with open(dst, "w", encoding="utf8", newline="\n") as fh:
        fh.write(out)

    a, b = nets_of(pcbnew.LoadBoard(src)), nets_of(pcbnew.LoadBoard(dst))
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
    print(f"fusion_ecad: {len(code)} nets ({len(rename)} renamed), {len(pads)} connected pads, "
          f"{len(a[1])} tracks/vias, {len(a[2])} zones: identical when loaded back; "
          "every board pad has its schematic net")
    z = shutil.make_archive(os.path.join(HW, "mech", "tredder-revA-fusion-ecad"), "zip", HW, "fusion_ecad")
    print("wrote", os.path.relpath(z, HW))


if __name__ == "__main__":
    main()
