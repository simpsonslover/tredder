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
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
HW = os.path.abspath(os.path.join(HERE, ".."))
OUT = os.path.join(HW, "fusion_ecad")
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
    return out, code


def nets_of(board):
    pads = {(fp.GetReference(), p.GetNumber(), p.GetPosition().x, p.GetPosition().y): p.GetNetname()
            for fp in board.GetFootprints() for p in fp.Pads()}
    tracks = sorted((t.GetClass(), t.GetStart().x, t.GetStart().y, t.GetEnd().x, t.GetEnd().y, t.GetNetname())
                    for t in board.GetTracks())
    zones = sorted((z.GetNetname(), z.GetLayer()) for z in board.Zones())
    return pads, tracks, zones


def main():
    src = os.path.join(HW, "wallet.kicad_pcb")
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)
    text = open(src, encoding="utf8").read()
    out, code = downgrade(text)
    dst = os.path.join(OUT, "wallet.kicad_pcb")
    with open(dst, "w", encoding="utf8", newline="\n") as fh:
        fh.write(out)
    for f in COPY:
        shutil.copy(os.path.join(HW, f), OUT)
    shutil.copytree(os.path.join(HW, "wallet.pretty"), os.path.join(OUT, "wallet.pretty"))
    shutil.copytree(os.path.join(HW, "3d"), os.path.join(OUT, "3d"))

    a, b = nets_of(pcbnew.LoadBoard(src)), nets_of(pcbnew.LoadBoard(dst))
    for what, x, y in zip(("pad", "track/via", "zone"), a, b):
        if x != y:
            sys.exit(f"export_fusion_ecad: {what} nets differ after conversion")
    connected = sum(1 for n in a[0].values() if n)
    print(f"fusion_ecad: {len(code)} nets, {connected} connected pads, {len(a[1])} tracks/vias, "
          f"{len(a[2])} zones: identical when loaded back")
    z = shutil.make_archive(os.path.join(HW, "mech", "tredder-revA-fusion-ecad"), "zip", HW, "fusion_ecad")
    print("wrote", os.path.relpath(z, HW))


if __name__ == "__main__":
    main()
