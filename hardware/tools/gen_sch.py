"""
Generate wallet.kicad_sch + wallet.kicad_sym from design.py.

Output is KiCad 8 file format (20231120) so it opens in KiCad 8, 9 and 10.
Style: each symbol pin gets a short stub and a net label (label-based
netlist). It is less pretty than hand-drawn wiring but every connection is
explicit, greppable and generated from the single source of truth.
"""
import os
import uuid

import design as D

OUT = os.path.join(os.path.dirname(__file__), "..")
NS = uuid.UUID("6f2b0a4e-8c1d-4c55-9a51-0d3c3a1e7a10")  # stable UUIDs between runs
G = 2.54
PIN_LEN = 2.54
ROOT_UUID = str(uuid.uuid5(NS, "root-sheet"))


def uid(*k):
    return str(uuid.uuid5(NS, "/".join(map(str, k))))


def q(s):
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def f(v):
    return f"{v:.4f}".rstrip("0").rstrip(".")


def eff(size=1.27, hide=False, justify=None):
    j = f" (justify {justify})" if justify else ""
    h = " hide" if hide else ""
    return f"(effects (font (size {size} {size})){j}{h})"


def geom(tname):
    t = D.TYPES[tname]
    rows = max(len(t["left"]), len(t["right"]), 1)
    W = t["w"] * G
    return t, rows, W


# ---------------------------------------------------------------- library symbols
def lib_symbol(tname, prefixed):
    t, rows, W = geom(tname)
    name = f"wallet:{tname}" if prefixed else tname
    top, bot = G, -rows * G
    hide_names = "" if t["show_pin_names"] else " hide"
    out = [f"(symbol {q(name)} (pin_numbers{'' if t['show_pin_names'] else ' hide'}) "
           f"(pin_names (offset 0.508){hide_names}) (exclude_from_sim no) (in_bom yes) (on_board yes)"]
    out.append(f"(property \"Reference\" {q(t['prefix'])} (at 0 {f(top + 1.27)} 0) {eff()})")
    out.append(f"(property \"Value\" {q(tname)} (at 0 {f(bot - 1.27)} 0) {eff()})")
    out.append(f"(property \"Footprint\" \"\" (at 0 0 0) {eff(hide=True)})")
    out.append(f"(property \"Datasheet\" \"\" (at 0 0 0) {eff(hide=True)})")
    out.append(f"(property \"Description\" {q(tname)} (at 0 0 0) {eff(hide=True)})")
    out.append(f"(symbol {q(tname + '_0_1')} (rectangle (start {f(-W/2)} {f(top)}) (end {f(W/2)} {f(bot)}) "
               f"(stroke (width 0.254) (type default)) (fill (type background))))")
    pins = []
    for i, (num, nm) in enumerate(t["left"]):
        pins.append(f"(pin passive line (at {f(-W/2 - PIN_LEN)} {f(-i*G)} 0) (length {PIN_LEN}) "
                    f"(name {q(nm)} {eff()}) (number {q(num)} {eff()}))")
    for i, (num, nm) in enumerate(t["right"]):
        pins.append(f"(pin passive line (at {f(W/2 + PIN_LEN)} {f(-i*G)} 180) (length {PIN_LEN}) "
                    f"(name {q(nm)} {eff()}) (number {q(num)} {eff()}))")
    out.append(f"(symbol {q(tname + '_1_1')} " + " ".join(pins) + ")")
    out.append(")")
    return "\n".join(out)


def pin_points(tname, sx, sy):
    """Schematic coordinates (y down) of each pin's connection point and its outward direction."""
    t, rows, W = geom(tname)
    pts = {}
    for i, (num, _) in enumerate(t["left"]):
        pts[num] = (sx - W/2 - PIN_LEN, sy + i*G, -1)
    for i, (num, _) in enumerate(t["right"]):
        pts[num] = (sx + W/2 + PIN_LEN, sy + i*G, +1)
    return pts


# ---------------------------------------------------------------- layout
BLOCKS = {  # group: (x, y, max_height)
    "mcu": (15, 35, 150),
    "se": (125, 35, 150),
    "usb": (255, 35, 150),
    "power": (385, 35, 160),
    "sd": (125, 205, 150),
    "lcd": (255, 205, 100),
    "buttons": (385, 220, 150),
    "debug": (15, 300, 90),
    "mech": (255, 320, 60),
}
COL_W = 62
LABEL_SPACE = 24


def layout():
    pos = {}
    for g, (bx, by, bh) in BLOCKS.items():
        refs = [r for r in D.P if D.P[r]["group"] == g]
        cx, cy = bx, by + 8
        for r in refs:
            t, rows, W = geom(D.P[r]["type"])
            h = rows * G + 12
            if cy + h > by + bh and cy > by + 8:
                cx += COL_W if D.P[r]["type"] != "PICO2W" else 110
                cy = by + 8
            sx = cx + LABEL_SPACE + W/2 + PIN_LEN
            sx = round(sx / G) * G
            sy = round((cy + 5) / G) * G
            pos[r] = (sx, sy)
            cy += h
    return pos


# ---------------------------------------------------------------- schematic
def build():
    pin_net = D.check()
    pos = layout()
    used_types = sorted({D.P[r]["type"] for r in D.P})

    s = ['(kicad_sch (version 20231120) (generator "eeschema") (generator_version "8.0")',
         f'(uuid {q(ROOT_UUID)})', '(paper "A2")',
         '(title_block (title "Hardware wallet - Phase 1 main board") (date "2026-10-31") (rev "A")'
         ' (comment 1 "Generated from hardware/tools/design.py - edit there, not here")'
         ' (comment 2 "RP2350 (Pico 2 W) + OPTIGA Trust M V3 + ST7789 + microSD + LiPo"))',
         "(lib_symbols"]
    s += [lib_symbol(t, True) for t in used_types]
    s.append(")")

    for g, (bx, by, _) in BLOCKS.items():
        s.append(f"(text {q(D.GROUP_TITLES[g])} (exclude_from_sim no) (at {bx} {by} 0) "
                 f"{eff(2.54, justify='left bottom')} (uuid {q(uid('title', g))}))")

    notes = [
        "SECURITY NOTES",
        "1. Seed is never stored in plain form: Pico flash (W25Q) is external and readable by desoldering.",
        "2. SE VCC switched by Q2 (default off). Use OPTIGA Shielded Connection (encrypted I2C) - pairing secret in RP2350 OTP.",
        "3. No test points on SE_SDA/SE_SCL on purpose. SWD: Tag-Connect footprint only, disabled via OTP in production.",
        "4. R3/R4 DNP = charge-only USB (removes USB attack surface; updates via signed image on microSD).",
        "5. microSD is untrusted input: separate SPI bus (SPI0; display on SPI1), power-gated by Q3, unpowered unless needed.",
        "6. SW1 pole B grounds 3V3_EN: device is fully off even with USB attached; charging still works.",
    ]
    for i, n in enumerate(notes):
        s.append(f"(text {q(n)} (exclude_from_sim no) (at 255 {360 + i*5} 0) "
                 f"{eff(1.8 if i else 2.54, justify='left bottom')} (uuid {q(uid('note', i))}))")

    for ref, (sx, sy) in pos.items():
        p = D.P[ref]
        tname = p["type"]
        t, rows, W = geom(tname)
        dnp = "yes" if p["dnp"] else "no"
        in_bom = "no" if p["dnp"] and tname in ("TESTPOINT", "HOLE", "TC2030") else "yes"
        s.append(f"(symbol (lib_id {q('wallet:' + tname)}) (at {f(sx)} {f(sy)} 0) (unit 1) "
                 f"(exclude_from_sim no) (in_bom {in_bom}) (on_board yes) (dnp {dnp}) "
                 f"(uuid {q(uid('sym', ref))})")
        s.append(f"(property \"Reference\" {q(ref)} (at {f(sx)} {f(sy - G - 1.27)} 0) {eff()})")
        s.append(f"(property \"Value\" {q(p['value'])} (at {f(sx)} {f(sy + (rows - 1)*G + G + 2.0)} 0) {eff()})")
        s.append(f"(property \"Footprint\" {q(p['footprint'])} (at {f(sx)} {f(sy)} 0) {eff(hide=True)})")
        s.append(f"(property \"Datasheet\" \"\" (at {f(sx)} {f(sy)} 0) {eff(hide=True)})")
        s.append(f"(property \"Description\" {q(p['note'])} (at {f(sx)} {f(sy)} 0) {eff(hide=True)})")
        s.append(f"(property \"MPN\" {q(p['mpn'])} (at {f(sx)} {f(sy)} 0) {eff(hide=True)})")
        s.append(f"(property \"LCSC\" {q(p['lcsc'])} (at {f(sx)} {f(sy)} 0) {eff(hide=True)})")
        for num, _ in t["left"] + t["right"]:
            s.append(f"(pin {q(num)} (uuid {q(uid('pin', ref, num))}))")
        s.append(f"(instances (project \"wallet\" (path {q('/' + ROOT_UUID)} (reference {q(ref)}) (unit 1))))")
        s.append(")")

        for num, (px, py, d) in pin_points(tname, sx, sy).items():
            rp = f"{ref}.{num}"
            if rp in D.NC:
                s.append(f"(no_connect (at {f(px)} {f(py)}) (uuid {q(uid('nc', rp))}))")
                continue
            ex = px + d * G
            s.append(f"(wire (pts (xy {f(px)} {f(py)}) (xy {f(ex)} {f(py)})) "
                     f"(stroke (width 0) (type default)) (uuid {q(uid('w', rp))}))")
            just = "left" if d > 0 else "right"
            ang = 0 if d > 0 else 180
            s.append(f"(global_label {q(pin_net[rp])} (shape passive) (at {f(ex)} {f(py)} {ang}) "
                     f"(fields_autoplaced yes) {eff(justify=just)} (uuid {q(uid('l', rp))}) "
                     f"(property \"Intersheetrefs\" \"${{INTERSHEET_REFS}}\" (at {f(ex)} {f(py)} 0) "
                     f"{eff(justify=just, hide=True)}))")

    s.append('(sheet_instances (path "/" (page "1")))')
    s.append(")")
    with open(os.path.join(OUT, "wallet.kicad_sch"), "w", encoding="utf8", newline="\n") as fh:
        fh.write("\n".join(s) + "\n")

    lib = ['(kicad_symbol_lib (version 20231120) (generator "wallet_gen") (generator_version "8.0")']
    lib += [lib_symbol(t, False) for t in sorted(D.TYPES)]
    lib.append(")")
    with open(os.path.join(OUT, "wallet.kicad_sym"), "w", encoding="utf8", newline="\n") as fh:
        fh.write("\n".join(lib) + "\n")
    return pos


if __name__ == "__main__":
    pos = build()
    print(f"schematic: {len(pos)} symbols written")
