"""
Breadboard prototype of the tredder board: same Pico pins as the PCB, so the same firmware runs
on both. Writes hardware/breadboard/breadboard.png (the wiring picture) and BREADBOARD.md
(parts, wire-by-wire table, what the breadboard can and cannot test).

Everything the picture shows is first laid out as data by layout(): every part leg in a hole and
every wire between two points. The same data is then
  - checked by check_layout(): every hole exists and holds one leg or wire, the connections the
    breadboard strips + wires make are exactly the intended netlist (intended()), each Pico pin
    carries the net the PCB gives it, and each button works whichever way round it is pushed in;
  - simulated by hardware/sim/breadboard.py (ngspice, same models as the PCB simulation);
  - drawn.
So the picture can't show a connection the tests haven't seen.

  python tools/gen_breadboard.py
"""
import os
from urllib.parse import quote_plus

from PIL import Image, ImageDraw, ImageFont

import design as D

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "breadboard")
PICO = dict(D.TYPES["PICO2W"]["left"] + D.TYPES["PICO2W"]["right"])     # pin number -> name
PICO_GND = [p for p in map(str, range(1, 41)) if PICO[p] in ("GND", "AGND")]

# ------------------------------------------------------------------ what connects to what
# Terminals are "MODULE.PIN" (A1 = the Pico by physical pin number; R10.1, D1.A, SW_UP.1 ... = parts
# on the breadboard). Pins must match board_pins.h / design.py.
SD, SE, LCD, CHG, BAT = "SD", "SE", "LCD", "CHG", "BAT"
NAME = {SD: "microSD module", SE: "SE050 kit", LCD: "Display", CHG: "Charger", BAT: "Battery"}
MODULE_PINS = {SD: ["VCC", "GND", "MISO", "MOSI", "SCK", "CS"], SE: ["3.3V", "GND", "SCL", "SDA"],
               LCD: ["BL", "CS", "DC", "RST", "SDA", "SCL", "VCC", "GND"],
               CHG: ["IN+", "IN-", "B+", "B-", "OUT+", "OUT-"]}
# (pico pin, module, module pin, colour, what it is)
WIRES = [
    ("1", SD, "MISO", "orange", "SD_MISO (SPI0 RX)"),
    ("2", SD, "CS", "orange", "SD_CS"),
    ("4", SD, "SCK", "orange", "SD_SCK (SPI0 SCK)"),
    ("5", SD, "MOSI", "orange", "SD_MOSI (SPI0 TX)"),
    ("11", SE, "SDA", "blue", "SE_SDA (I2C0 SDA)"),
    ("12", SE, "SCL", "yellow", "SE_SCL (I2C0 SCL)"),
    ("31", LCD, "SCL", "purple", "LCD_SCK (SPI1 SCK)"),
    ("32", LCD, "SDA", "purple", "LCD_MOSI (SPI1 TX)"),
    ("26", LCD, "RST", "purple", "LCD_RST"),
    ("29", LCD, "DC", "purple", "LCD_DC"),
    ("27", LCD, "CS", "purple", "LCD_CS"),
    ("40", CHG, "IN+", "red", "VBUS: the Pico's USB 5 V feeds the charger"),
]
BUTTONS = [("16", "BACK"), ("17", "CONFIRM"), ("19", "UP"), ("20", "DOWN")]   # GP12, GP13, GP14, GP15
POWER = [  # module pin -> rail
    (SD, "VCC", "3V3"), (SD, "GND", "GND"), (SE, "3.3V", "3V3"), (SE, "GND", "GND"),
    (LCD, "VCC", "3V3"), (LCD, "GND", "GND"), (LCD, "BL", "3V3"), (CHG, "OUT-", "GND"),
]
# copper inside the modules (not wires you place):
# and all of the Pico's GND pins are one net
INTERNAL = [("A1.38", f"A1.{p}") for p in PICO_GND if p != "38"] + [
    # TP4056 + DW01 module: B+ and OUT+ are one copper net; IN- and OUT- too (the protection MOSFETs
    # sit between B- and OUT-, so the battery's minus must NOT touch the GND rail)
    ("CHG.B+", "CHG.OUT+"), ("CHG.IN-", "CHG.OUT-")]


def pcb_net(pin):
    return PIN_NET.get(f"A1.{pin}")


PIN_NET = D.check()


def intended():
    """The netlist the breadboard must make: net -> terminals. Every other terminal stays unconnected.
    Nets that reach a Pico pin carry the PCB's name for that pin (check_layout() compares)."""
    n = {
        "GND": [f"A1.{p}" for p in PICO_GND] + ["SD.GND", "SE.GND", "LCD.GND", "CHG.OUT-", "CHG.IN-",
                                                "R11.2", "SW1.3"] + [f"SW_{b}.4" for _, b in BUTTONS],
        "+3V3": ["A1.36", "SD.VCC", "SE.3.3V", "LCD.VCC", "LCD.BL"],
        "VBUS": ["A1.40", "CHG.IN+"],
        "VBAT": ["CHG.B+", "CHG.OUT+", "BAT.+", "D1.A", "R10.1"],
        "BAT_MINUS": ["BAT.-", "CHG.B-"],   # only through the module's protection switch to GND
        "VSYS": ["A1.39", "D1.K"],
        "VBAT_SENSE": ["A1.34", "R10.2", "R11.1"],
        # on/off: the PCB's SW1 pole B. Slid to the GND leg, 3V3_EN is pulled low and the Pico's 3.3 V
        # regulator (and everything on the 3.3 V rail) is off, even on USB. No resistor: the Pico has
        # its own pull-up on 3V3_EN. (SW1.1, the other outer leg, stays unconnected.)
        "3V3_EN": ["A1.37", "SW1.2"],
    }
    for pin, mod, mpin, _, what in WIRES:
        if mod != CHG:
            n[what.split()[0]] = [f"A1.{pin}", f"{mod}.{mpin}"]
    # GP6 (SD_DET) stays unconnected: the module has no card-detect pin and the firmware pulls GP6 up internally
    for pin, b in BUTTONS:
        n[f"BTN_{b}"] = [f"A1.{pin}", f"SW_{b}.1"]   # no pull-up resistor: the Pico's internal pull-up
    return n


# ------------------------------------------------------------------ breadboard geometry
P = 34                      # breadboard hole pitch, px
BX, BY = 120, 560           # breadboard top-left
COLS = 63
ROWS = "abcdefghij"
RAILS = ("+t", "-t", "+b", "-b")
COL = {"orange": (245, 140, 30), "white": (235, 235, 235), "blue": (40, 120, 230), "yellow": (240, 210, 40),
       "purple": (160, 80, 220), "red": (220, 40, 40), "black": (25, 25, 25), "green": (40, 170, 70),
       "grey": (150, 150, 150)}
W, H = BX * 2 + COLS * P + 80, 1780
try:
    F = ImageFont.truetype("arialbd.ttf", 19)
    FS = ImageFont.truetype("arial.ttf", 15)
    FT = ImageFont.truetype("arialbd.ttf", 30)
except OSError:
    F = FS = FT = ImageFont.load_default()


def hole_exists(col, row):
    """Rails have a gap every 6th position (groups of 5 holes, as on real boards)."""
    if not 1 <= col <= COLS:
        return False
    return col % 6 != 0 if row in RAILS else row in ROWS


def hole(col, row):
    """Pixel centre of breadboard hole (col 1..63, row 'a'..'j' or rail '+t','-t','+b','-b')."""
    x = BX + 40 + (col - 1) * P
    rail = {"+t": BY + 18, "-t": BY + 18 + P, "+b": BY + 18 + P * 2 + 12 * P + 40 + P, "-b": BY + 18 + P * 2 + 12 * P + 40}
    if row in rail:
        return x, rail[row]
    i = ROWS.index(row)
    y = BY + 18 + P * 2 + 30 + i * P + (P if i >= 5 else 0)   # one blank pitch for the centre channel
    return x, y


def strip(h):
    """Which metal strip a hole is on: rows a-e and f-j of a column, or a whole rail."""
    c, r = h
    return ("rail", r) if r in RAILS else ("strip", c, "top" if r in "abcde" else "bottom")


PICO_COL = 4                    # pin 1 / pin 40 column (the USB end)


def pico_hole(pin):
    """Pico seen from above, USB on the left: pins count anticlockwise, like any DIP part, so
    pins 1-20 run left to right along the BOTTOM row and 21-40 right to left along the TOP row.
    (check_layout() compares this handedness with the PCB's Pico footprint.)"""
    n = int(pin)
    return (PICO_COL + n - 1, "h") if n <= 20 else (PICO_COL + (40 - n), "c")


def wire_end(pin):
    """Where a jumper goes in for this Pico pin: row j (bottom pins 1-20) or row a (top pins 21-40), same column."""
    n = int(pin)
    return (PICO_COL + n - 1, "j") if n <= 20 else (PICO_COL + (40 - n), "a")


class Module:
    def __init__(self, name, title, x, y, pins, side, fill, w=None, note=""):
        self.name, self.title, self.pins, self.side = name, title, pins, side
        self.x, self.y = x, y
        self.w = w or max(220, len(pins) * 70 + 40)
        self.h = 120
        self.fill, self.note = fill, note

    def pin_xy(self, p):
        i = self.pins.index(p)
        x = self.x + 40 + i * ((self.w - 80) / max(1, len(self.pins) - 1))
        y = self.y + (self.h if self.side == "bottom" else 0)
        return x, y

    def draw(self, d):
        d.rounded_rectangle([self.x, self.y, self.x + self.w, self.y + self.h], 12, fill=self.fill, outline=(40, 40, 40), width=3)
        d.text((self.x + 14, self.y + (14 if self.side == "bottom" else 60)), self.title, fill=(255, 255, 255), font=F)
        if self.note:
            d.text((self.x + 14, self.y + (40 if self.side == "bottom" else 86)), self.note, fill=(230, 230, 230), font=FS)
        for p in self.pins:
            x, y = self.pin_xy(p)
            d.rectangle([x - 8, y - 8, x + 8, y + 8], fill=(210, 180, 80), outline=(90, 70, 20))
            d.text((x - 14, y + (-34 if self.side == "bottom" else 14)), p, fill=(255, 255, 255), font=FS)


MODS = {
    SD: Module(SD, "microSD card module", BX + 30, 1490, MODULE_PINS[SD], "top", (60, 90, 140),
               note="6-pin SPI module, no card detect"),
    SE: Module(SE, "NXP SE050 kit (OM-SE050ARD)", BX + 640, 1490, MODULE_PINS[SE], "top", (110, 60, 140),
               note="Arduino-header pins, jumper wires"),
    LCD: Module(LCD, "2.0in ST7789 display (ZJY200S10Z0TG01)", BX + 520, 150, MODULE_PINS[LCD], "bottom",
                (40, 40, 60), w=760, note="pin order as printed on the module"),
    CHG: Module(CHG, "LiPo charger TP4056 + protection", BX + 1300, 150, MODULE_PINS[CHG], "bottom", (40, 80, 150), w=470,
                note="1 A (R3 1.2k): battery >= 1000 mAh"),
}
A_COL, K_COL, TAP_COL, GND_COL = 48, 51, 54, 58   # battery + column (diode anode), cathode, divider tap, divider bottom
BAT_BOX = (BX + 1800, 120, BX + 2190, 250)
BAT_XY = {"+": (BAT_BOX[0] + 60, BAT_BOX[3]), "-": (BAT_BOX[0] + 120, BAT_BOX[3])}


def xy(p):
    """Pixel position of a wire end: a hole (col, row) or a terminal 'MODULE.PIN'."""
    if isinstance(p, tuple):
        return hole(*p)
    mod, pin = p.split(".", 1)
    if mod == BAT:
        return BAT_XY[pin]
    return MODS[mod].pin_xy(pin)


# ------------------------------------------------------------------ layout (the data everything else uses)
class Layout:
    def __init__(self):
        self.legs = []          # (terminal, hole)
        self.wires = []         # dict(a, b, colour, lane, width); a/b = hole or terminal

    def used(self):
        return {h for _, h in self.legs} | {p for w in self.wires for p in (w["a"], w["b"]) if isinstance(p, tuple)}

    def leg(self, term, h):
        self.legs.append((term, h))

    def wire(self, a, b, colour, lane=None, width=6):
        self.wires.append(dict(a=a, b=b, colour=colour, lane=lane, width=width))

    def rail_hole(self, col, rail, avoid=()):
        """Nearest existing, unused rail hole to a column."""
        used = self.used()
        for c in sorted(range(col - 8, col + 9), key=lambda c: (abs(c - col), c)):
            if hole_exists(c, rail) and (c, rail) not in used and c not in avoid:
                return (c, rail)
        raise ValueError(f"no free {rail} hole near column {col}")


BTN_COL0 = 30
SW1_COLS = (25, 26, 27)   # on/off switch legs (row c)


# breadboard resistors: (layout name, value, what it does)
# (the buttons and GP6 use the Pico's internal pull-ups instead of the PCB's R26-R29 and R20)
RESISTORS = [("R10", "4.7k", "battery divider top"), ("R11", "4.7k", "battery divider bottom")]


def btn_col(i):
    return BTN_COL0 + i * 4


def layout():
    L = Layout()
    for n in range(1, 41):
        L.leg(f"A1.{n}", pico_hole(n))
    ry = lambda r: hole(1, r)[1]  # noqa: E731
    # rails: top and bottom joined at the right end, fed from 3V3_OUT (36) and GND (38)
    L.wire((COLS - 1, "+t"), (COLS - 1, "+b"), "red", (ry("+t") + ry("+b")) // 2, 5)
    L.wire((COLS - 2, "-t"), (COLS - 2, "-b"), "black", (ry("-t") + ry("-b")) // 2 + 20, 5)
    L.wire(wire_end("36"), L.rail_hole(PICO_COL + 4, "+t"), "red", hole(*wire_end("36"))[1] - 22, 5)
    L.wire(wire_end("38"), L.rail_hole(PICO_COL + 2, "-t"), "black", hole(*wire_end("38"))[1] - 10, 5)

    # battery: diode, divider in the top half, next to VSYS (39) and GP28 (34)
    # (legs first, so the rail wires below pick other holes)
    L.leg("D1.A", (A_COL, "c"))
    L.leg("D1.K", (K_COL, "c"))
    L.leg("R10.1", (A_COL, "d"))
    L.leg("R10.2", (TAP_COL, "d"))
    L.leg("R11.1", (TAP_COL, "c"))
    L.leg("R11.2", (GND_COL, "c"))
    above = ry("+t") - 46
    L.wire("CHG.OUT+", (A_COL, "b"), "red", above - 30)             # charger OUT+ (= battery + behind the protection)
    L.wire((K_COL, "b"), wire_end("39"), "red", above)              # cathode to VSYS
    L.wire(wire_end("34"), (TAP_COL, "b"), "white", above - 15)     # divider tap to GP28
    L.wire((GND_COL, "a"), L.rail_hole(GND_COL, "-t"), "black", ry("-t") + 10, 4)
    # the battery's leads go to the charger's B+ / B- pads (soldered, or a pigtail with header pins)
    L.wire("BAT.+", "CHG.B+", "red", 300)
    L.wire("BAT.-", "CHG.B-", "black", 316)

    # on/off slide switch (SPDT): legs c25 c26 c27, middle to 3V3_EN (pin 37), c27 to the - rail
    for k, c in enumerate(SW1_COLS, 1):
        L.leg(f"SW1.{k}", (c, "c"))
    L.wire(wire_end("37"), (SW1_COLS[1], "a"), "grey", ry("+t") - 100)
    L.wire((SW1_COLS[2], "b"), L.rail_hole(SW1_COLS[2], "-t"), "black", ry("-t") + 12, 4)

    # buttons straddle the channel; signal on the bottom-left leg, GND on the diagonally opposite leg,
    # so the button works whichever way round it goes in. No pull-up resistor: the firmware turns on
    # the Pico's internal pull-up on GP12-GP15.
    for i, (pin, b) in enumerate(BUTTONS):
        c = btn_col(i)
        for k, h in enumerate([(c, "f"), (c + 2, "f"), (c, "e"), (c + 2, "e")], 1):
            L.leg(f"SW_{b}.{k}", h)
        L.wire(wire_end(pin), (c, "j"), "green", hole(*wire_end(pin))[1] + 30 + i * 12)
        L.wire((c + 2, "a"), L.rail_hole(c + 2, "-t"), "black", ry("-t") + 8, 5)


    # Pico -> modules: pins 21-40 up to the display/charger, pins 1-20 down to the SD module / SE kit
    nt = nb = 0
    for pin, mod, mpin, colour, _ in WIRES:
        if int(pin) > 20:
            lane, nt = 470 - nt * 14, nt + 1
        else:
            lane, nb = 1220 + nb * 14, nb + 1
        L.wire(wire_end(pin), f"{mod}.{mpin}", colour, lane)
    # module power pins -> nearest free rail hole (not under a signal wire)
    avoid = {wire_end(w[0])[0] for w in WIRES} | {c + d for c in (A_COL, K_COL, TAP_COL) for d in (-1, 0, 1)}
    for k, (mod, mpin, rail) in enumerate(POWER):
        bx, by = MODS[mod].pin_xy(mpin)
        top = MODS[mod].side == "bottom"
        r = ("+t" if rail == "3V3" else "-t") if top else ("+b" if rail == "3V3" else "-b")
        a = L.rail_hole(round((bx - BX - 40) / P) + 1, r, avoid)
        L.wire(a, f"{mod}.{mpin}", "red" if rail == "3V3" else "black", (hole(*a)[1] + by) // 2 + (k % 3) * 10)
    return L


# ------------------------------------------------------------------ checks
def connect(lay, links=()):
    """Union-find over strips, legs, wires and module copper. Returns terminal -> group id."""
    parent = {}

    def find(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a, b):
        parent[find(a)] = find(b)

    node = lambda p: strip(p) if isinstance(p, tuple) else p  # noqa: E731
    for term, h in lay.legs:
        union(term, strip(h))
    for w in lay.wires:
        union(node(w["a"]), node(w["b"]))
    for a, b in INTERNAL + list(links):
        union(a, b)
    terms = {t for t in parent if isinstance(t, str)} | {f"{m}.{p}" for m, ps in MODULE_PINS.items() for p in ps}
    terms |= {t for ts in intended().values() for t in ts}
    return {t: find(t) for t in terms}


def net_errors(lay, links=()):
    want = intended()
    net_of = {t: n for n, ts in want.items() for t in ts}
    grp = connect(lay, links)
    errs = []
    for n, ts in want.items():                       # every intended net in one piece
        main = max({grp[t] for t in ts}, key=lambda g: sum(grp[t] == g for t in ts))
        for t in ts:
            if grp[t] != main:
                errs.append(f"{t} is not connected to {n}")
    groups = {}
    for t, g in grp.items():
        groups.setdefault(g, []).append(t)
    for ts in groups.values():                       # and never two nets in one piece
        nets = sorted({net_of[t] for t in ts if t in net_of})
        if len(nets) > 1:
            errs.append("short between " + " and ".join(nets) + f" ({', '.join(sorted(ts))})")
        stray = sorted(t for t in ts if t not in net_of)
        if stray and len(ts) > 1:
            errs.append(f"{', '.join(stray)} should be unconnected but touch {', '.join(sorted(set(ts) - set(stray)))}")
    return errs


def pico_handedness_errors():
    """The first breadboard picture drew the Pico mirror-imaged (pins 1-20 on the wrong edge). Compare
    which way round its pins count with the PCB's own Pico footprint: a rotation keeps the sign of
    (pin1->pin20) x (pin1->pin40), a mirror flips it. Both are in y-down coordinates, seen from above."""
    import re
    fp = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "wallet.pretty",
                      D.P["A1"]["footprint"].split(":")[1] + ".kicad_mod")
    pads = {m.group(1): (float(m.group(2)), float(m.group(3)))
            for m in re.finditer(r'\(pad "(\d+)"[^(]*\(at ([-\d.]+) ([-\d.]+)', open(fp, encoding="utf8").read())}

    def turn(p1, p20, p40):
        return (p20[0] - p1[0]) * (p40[1] - p1[1]) - (p20[1] - p1[1]) * (p40[0] - p1[0])

    pcb = turn(pads["1"], pads["20"], pads["40"])
    bb = turn(*(hole(*pico_hole(n)) for n in ("1", "20", "40")))
    return [] if (pcb > 0) == (bb > 0) else ["the Pico is drawn mirror-imaged compared with the PCB footprint "
                                             "(pins 1-20 on the wrong edge)"]


def check_layout(lay):
    """All the ways a breadboard picture can be wrong. Returns a list of problems (empty = good)."""
    errs = pico_handedness_errors()
    taken = {}
    points = [(h, t) for t, h in lay.legs] + [(p, "a wire") for w in lay.wires for p in (w["a"], w["b"]) if isinstance(p, tuple)]
    for h, who in points:
        if not hole_exists(*h):
            errs.append(f"{who} goes into hole {h[0]}{h[1]}, which doesn't exist (rail gap)")
        elif h in taken:
            errs.append(f"hole {h[0]}{h[1]} holds both {taken[h]} and {who}")
        taken[h] = who
    for pin, _, _, _, what in WIRES:                 # Pico pins carry the PCB's net
        if pcb_net(pin) != what.split()[0].rstrip(":"):
            errs.append(f"Pico pin {pin}: breadboard says {what.split()[0]}, PCB has {pcb_net(pin)}")
    for n, ts in intended().items():
        for t in ts:
            if t.startswith("A1.") and pcb_net(t[3:]) != n:
                errs.append(f"{t} is on {n} here but on {pcb_net(t[3:])} on the PCB")
    errs += net_errors(lay)
    # a 4-leg tactile button joins its legs in pairs; which pairs depends on how it's turned.
    # Test both ways round: released = nothing extra, pressed = signal to GND.
    for name, pairs, press in (("legs along the channel joined", [(1, 2), (3, 4)], (1, 3)),
                               ("legs across the channel joined", [(1, 3), (2, 4)], (1, 2))):
        base = [(f"SW_{b}.{x}", f"SW_{b}.{y}") for _, b in BUTTONS for x, y in pairs]
        for e in net_errors(lay, base):
            if "should be unconnected" not in e:     # the idle legs join their pair: expected
                errs.append(f"button fitted with {name}, released: {e}")
        for _, b in BUTTONS:
            grp = connect(lay, base + [(f"SW_{b}.{press[0]}", f"SW_{b}.{press[1]}")])
            if grp[f"SW_{b}.1"] != grp["A1.38"]:
                errs.append(f"button {b} fitted with {name}: pressing it doesn't pull BTN_{b} to GND")
    return errs


def self_test():
    """The check must catch the mistake the first version of the picture had: battery - not wired."""
    lay = layout()
    lay.wires = [w for w in lay.wires if "BAT.-" not in (w["a"], w["b"])]
    errs = check_layout(lay)
    assert any("BAT_MINUS" in e or "BAT.-" in e for e in errs), errs  # either end may be named (set order)


# ------------------------------------------------------------------ drawing
def breadboard(d):
    x0, y0 = BX, BY
    x1 = BX + 80 + (COLS - 1) * P
    y1 = hole(1, "+b")[1] + 30
    d.rounded_rectangle([x0, y0 - 10, x1, y1], 18, fill=(238, 236, 228), outline=(180, 178, 170), width=3)
    ch = (hole(1, "e")[1] + hole(1, "f")[1]) // 2
    d.rectangle([x0, ch - 8, x1, ch + 8], fill=(210, 208, 200))
    for rail, colr in (("+t", "red"), ("-t", "blue"), ("+b", "red"), ("-b", "blue")):
        y = hole(1, rail)[1]
        dy = -13 if rail in ("+t", "-b") else 13
        d.line([x0 + 20, y + dy, x1 - 20, y + dy], fill=COL[colr], width=3)
        d.text((x0 + 4, y - 9), rail[0], fill=COL[colr], font=F)
    for c in range(1, COLS + 1):
        for r in list(ROWS) + list(RAILS):
            if hole_exists(c, r):
                x, y = hole(c, r)
                d.rectangle([x - 4, y - 4, x + 4, y + 4], fill=(70, 70, 70))
    for r in ROWS:
        x, y = hole(1, r)
        d.text((x0 + 6, y - 8), r, fill=(120, 120, 120), font=FS)


def pico(d):
    (x0, ya), (x1, yb) = hole(*pico_hole("1")), hole(*pico_hole("21"))
    y0, y1 = min(ya, yb), max(ya, yb)
    d.rounded_rectangle([x0 - 22, y0 - 20, x1 + 22, y1 + 20], 10, fill=(30, 120, 60), outline=(15, 70, 35), width=3)
    ux = x0 - 22
    d.rectangle([ux - 38, (y0 + y1) / 2 - 26, ux + 10, (y0 + y1) / 2 + 26], fill=(190, 190, 195), outline=(110, 110, 115))
    d.text((x0 + 30, (y0 + y1) / 2 - 30), "Raspberry Pi Pico 2 W", fill=(255, 255, 255), font=FT)
    d.text((x0 + 30, (y0 + y1) / 2 + 8), "USB here (power + programming)", fill=(220, 240, 220), font=FS)
    for pin in [str(i) for i in range(1, 41)]:
        x, y = hole(*pico_hole(pin))
        d.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(210, 180, 80), outline=(120, 90, 20))
        name = (PICO[pin].replace("_A0", "").replace("_A1", "").replace("_A2", "").replace("3V3_OUT", "3V3")
                .replace("ADC_VREF", "VREF").replace("3V3_EN", "3V3EN"))
        top = int(pin) > 20
        vtext(x, y + (12 if top else -12), f"{pin} {name}", top)


CANVAS = [None]


def vtext(x, y, s, down):
    """Vertical label starting at (x, y), reading downwards (top row) or upwards (bottom row)."""
    d = ImageDraw.Draw(CANVAS[0])
    w, h = int(d.textlength(s, font=FS)) + 4, 18
    tile = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(tile).text((2, 0), s, fill=(255, 255, 255), font=FS)
    tile = tile.rotate(-90 if down else 90, expand=True)
    CANVAS[0].paste(tile, (int(x - tile.width / 2), int(y if down else y - tile.height)), tile)


def route(d, a, b, lane, colour, width=6):
    """Orthogonal wire: from a, vertically to lane y, across, vertically to b (straight if no lane)."""
    pts = [a, b] if lane is None else [a, (a[0], lane), (b[0], lane), b]
    d.line(pts, fill=(20, 20, 20), width=width + 3, joint="curve")
    d.line(pts, fill=COL[colour], width=width, joint="curve")
    for p in (a, b):
        d.ellipse([p[0] - 6, p[1] - 6, p[0] + 6, p[1] + 6], fill=COL[colour], outline=(20, 20, 20))


def resistor(d, a, b):
    d.line([a, b], fill=(120, 120, 120), width=3)
    mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    if abs(a[0] - b[0]) < abs(a[1] - b[1]):
        d.rounded_rectangle([mx - 8, my - 18, mx + 8, my + 18], 6, fill=(225, 200, 150), outline=(120, 90, 50))
        d.line([mx - 8, my - 6, mx + 8, my - 6], fill=(140, 80, 20), width=3)
    else:
        d.rounded_rectangle([mx - 18, my - 8, mx + 18, my + 8], 6, fill=(225, 200, 150), outline=(120, 90, 50))
        d.line([mx - 6, my - 8, mx - 6, my + 8], fill=(140, 80, 20), width=3)


def draw(lay):
    img = Image.new("RGB", (W, H), (250, 250, 247))
    CANVAS[0] = img
    d = ImageDraw.Draw(img)
    d.text((BX, 18), "tredder breadboard prototype: same pins as the PCB (rev A)", fill=(20, 20, 20), font=FT)
    d.text((BX, 58), "Wire colours: orange = microSD SPI, blue/yellow = security chip I2C, purple = display SPI, green = buttons, "
                     "red = +3.3 V / 5 V / battery +, black = GND, white = signals.  Full list: BREADBOARD.md",
           fill=(60, 60, 60), font=FS)
    d.text((BX, 80), "Rail holes come in groups of 5: use only the holes shown. If your board's rails are split in the "
                     "middle, bridge each rail across the gap.", fill=(60, 60, 60), font=FS)
    breadboard(d)
    pico(d)
    for m in MODS.values():
        m.draw(d)
    leg = {t: hole(*h) for t, h in lay.legs}

    # the battery, wired straight into the breadboard
    d.rounded_rectangle(BAT_BOX, 10, fill=(150, 150, 165), outline=(60, 60, 70), width=3)
    d.text((BAT_BOX[0] + 120, BAT_BOX[1] + 30), "BT1  1S LiPo, 1100 mAh", fill=(20, 20, 20), font=F)
    d.text((BAT_BOX[0] + 160, BAT_BOX[1] + 56), "to the charger's B+ / B-", fill=(20, 20, 20), font=FS)
    d.text((BAT_BOX[0] + 160, BAT_BOX[1] + 76), "check red = + first!", fill=(150, 20, 20), font=FS)
    for s, p in BAT_XY.items():
        d.text((p[0] + 8, p[1] + 4), s, fill=(20, 20, 20), font=F)

    # parts on the breadboard
    a, k = leg["D1.A"], leg["D1.K"]
    d.rectangle([a[0] + 6, a[1] - 8, k[0] - 6, k[1] + 8], fill=(30, 30, 30))
    d.rectangle([k[0] - 22, k[1] - 10, k[0] - 8, k[1] + 10], fill=(215, 215, 220), outline=(90, 90, 90), width=2)  # cathode band (stripe), big on purpose
    d.line([a, (a[0] + 6, a[1])], fill=(120, 120, 120), width=3)
    d.line([k, (k[0] - 6, k[1])], fill=(120, 120, 120), width=3)
    dx, dy = hole(A_COL, "c")
    d.rounded_rectangle([dx - 128, dy - 12, dx - 16, dy + 12], 4, fill=(255, 250, 220), outline=(140, 120, 60))
    d.text((dx - 124, dy - 11), "D1 1N5817", fill=(20, 20, 20), font=F)
    for ref, _, _ in RESISTORS:
        resistor(d, leg[f"{ref}.1"], leg[f"{ref}.2"])
    for i, (_, b) in enumerate(BUTTONS):
        xs = [leg[f"SW_{b}.{k}"][0] for k in range(1, 5)]
        ys = [leg[f"SW_{b}.{k}"][1] for k in range(1, 5)]
        tl, br = (min(xs), min(ys)), (max(xs), max(ys))
        d.rectangle([tl[0] - 14, tl[1] - 12, br[0] + 14, br[1] + 12], fill=(40, 40, 40), outline=(10, 10, 10), width=2)
        d.ellipse([(tl[0] + br[0]) / 2 - 14, (tl[1] + br[1]) / 2 - 14, (tl[0] + br[0]) / 2 + 14, (tl[1] + br[1]) / 2 + 14],
                  fill=(200, 200, 200))
        for k in range(1, 5):
            x, y = leg[f"SW_{b}.{k}"]
            d.ellipse([x - 5, y - 5, x + 5, y + 5], fill=(200, 200, 200), outline=(10, 10, 10))
        d.text((tl[0] - 14, br[1] + 14), b, fill=(20, 20, 20), font=F)

    # on/off slide switch
    l1, l3 = leg["SW1.1"], leg["SW1.3"]
    d.rounded_rectangle([l1[0] - 16, l1[1] - 16, l3[0] + 16, l3[1] + 16], 5, fill=(30, 30, 30), outline=(10, 10, 10), width=2)
    d.rectangle([l3[0] - 12, l3[1] - 7, l3[0] + 6, l3[1] + 7], fill=(230, 230, 230))     # knob, slid to the GND end = OFF
    for k in (1, 2, 3):
        x, y = leg[f"SW1.{k}"]
        d.ellipse([x - 4, y - 4, x + 4, y + 4], fill=(200, 200, 200))
    sx, sy = l1[0] - 16, l1[1] + 22
    d.rounded_rectangle([sx, sy, sx + 238, sy + 46], 4, fill=(255, 250, 220), outline=(140, 120, 60))
    d.text((sx + 4, sy + 1), "SW1 on/off switch", fill=(20, 20, 20), font=F)
    d.text((sx + 4, sy + 24), "knob towards c27 (GND) = OFF", fill=(20, 20, 20), font=FS)

    for w in lay.wires:
        route(d, xy(w["a"]), xy(w["b"]), w["lane"], w["colour"], w["width"])
    # the diode's direction, spelled out (a reversed diode cost a long debugging session on the
    # user's breadboard): which wire goes on which end
    def tag(x, y, text, colour=(20, 20, 20)):
        tw = d.textlength(text, font=F)
        d.rounded_rectangle([x - 4, y - 2, x + tw + 4, y + 22], 4, fill=(255, 250, 220), outline=(140, 120, 60))
        d.text((x, y), text, fill=colour, font=F)
    ax_, ay_ = hole(A_COL, "a")
    kx_, ky_ = hole(K_COL, "a")
    tag(ax_ - 200, ay_ - 52, "from charger OUT+")
    tag(kx_ + 14, ay_ - 52, "to Pico pin 39 (VSYS)")
    cx_, cy_ = hole(K_COL, "c")
    tag(hole(A_COL, "d")[0] - 330, cy_ + 26, "diode: STRIPE end -> pin 39", (170, 20, 20))

    # resistor labels last, on top of the wires: PCB reference + value
    for ref, value, what in RESISTORS:
        (ax, ay), (bx, by) = leg[f"{ref}.1"], leg[f"{ref}.2"]
        mx, my = (ax + bx) / 2, (ay + by) / 2
        s = f"{ref} {value}"
        tw = d.textlength(s, font=F)
        if abs(ax - bx) < abs(ay - by):          # standing resistor: label to its right
            x, y = mx + 14, my - 11
        else:                                    # lying resistor: label above (R10) or below (R11) its body
            x, y = mx - tw / 2, (my + 12 if ref == "R10" else my - 34)
        d.rounded_rectangle([x - 4, y - 2, x + tw + 4, y + 22], 4, fill=(255, 250, 220), outline=(140, 120, 60))
        d.text((x, y), s, fill=(20, 20, 20), font=F)
    return img


# ------------------------------------------------------------------ BREADBOARD.md
def amazon(q):
    """Amazon search link (not a fixed listing: listings change and many are resellers)."""
    return f"[Amazon](https://www.amazon.com/s?k={quote_plus(q)})"


def gpio(pin):
    return PICO[pin].split("_")[0]


def hname(h):
    c, r = h
    return {"+t": f"top + rail", "-t": "top - rail", "+b": "bottom + rail", "-b": "bottom - rail"}.get(r, f"{r}{c}")


def markdown(lay):
    leg = dict(lay.legs)
    L = ["# Breadboard prototype (same pins as the PCB)", "",
         "Generated by `hardware/tools/gen_breadboard.py` (picture: `breadboard.png`). Every signal uses the same "
         "Pico pin as the PCB, so the firmware built for the board runs here unchanged. Pin numbers are the Pico's "
         "physical pins (1-40), with its USB end on the left. Seen from above like that, **pin 1 is at the "
         "bottom left**: pins 1-20 run left to right along the bottom edge and 21-40 come back along the top "
         "(pin numbers are printed on the underside of the Pico). Hole names are row letter + column number "
         "(column 1 at the left).", "",
         "**Tested before you build it:** the generator checks the picture's own data: every hole exists and holds "
         "one thing, the strips and wires make exactly the intended netlist (no missing connection, no short), each "
         "Pico pin carries the same net as on the PCB, and each button works either way round. "
         "`hardware/sim/breadboard.py` then simulates that wiring in ngspice "
         "(battery, USB charging, worst-case load, I2C, buttons): results in "
         "[TEST_REPORT.md](TEST_REPORT.md).", "",
         "![breadboard](breadboard.png)", "",
         "## Parts", "",
         "| Qty | Part | Notes | Buy (Amazon search) |", "|---|---|---|---|",
         "| 1 | Raspberry Pi Pico 2 W **with headers** (\"Pico 2 WH\") | Same module as on the PCB | " + amazon('Raspberry Pi Pico 2 WH pre-soldered headers') + " |",
         "| 1-2 | Full-size breadboard (830 holes) + jumper wires (male-male and male-female) | | |",
         "| 1 | 2.0in 240x320 IPS display, ST7789V, 8-pin **BL CS DC RST SDA SCL VCC GND** (e.g. ZJY200S10Z0TG01) | The same module the PCB uses (the PCB lists the same 8 pins from the other end: pin 1 = GND ... pin 8 = BL) | " + amazon('2.0 inch IPS LCD 240x320 ST7789 SPI 8 pin') + " |",
         "| 1 | NXP SE050 development kit **OM-SE050ARD** (Arduino-shield format, SE050C2 configuration) | The PCB's U2 is the bare SE050C2 chip. Wire it with male-female jumpers from its Arduino header (SDA, SCL, 3.3V, GND). Leave its I2C pull-up jumpers J37/J38 at the default 3.3 k and add none; set the supply jumper J19 for 3.3 V from the header (NXP AN12395, power settings table) | " + amazon("NXP OM-SE050ARD SE050 development kit") + " (often not stocked there: [Mouser](https://www.mouser.com/c/?q=OM-SE050ARD), [Digi-Key](https://www.digikey.com/en/products/result?keywords=OM-SE050ARD)) |",
         "| 1 | microSD card module, 6 pins **VCC GND MISO MOSI SCK CS** (SPI, no card-detect pin) | VCC goes to the 3.3 V rail. If yours has a 3-pin regulator chip (often AMS1117-3.3) and the card won't start, see \"microSD module power\" below | " + amazon('micro SD card module SPI 6 pin') + " |",
         "| 4 | 6 mm tactile push buttons (4 legs) | BACK, CONFIRM, UP, DOWN | " + amazon('6x6mm tactile push button switch 4 pin through hole') + " |",
         "| 1 | Mini slide switch, SPDT, 3 legs, 2.54 mm pitch (e.g. SS12D00) | SW1, on/off (as on the PCB: it grounds 3V3_EN) | " + amazon('SS12D00 mini slide switch SPDT 3 pin breadboard') + " |",
         "| 2 | 4.7 k resistors | Battery-voltage divider into GP28 (as on the PCB: R10, R11) | " + amazon('4.7k ohm resistor 1/4W metal film') + " |",
         "| 1 | TP4056 LiPo charger module **with protection** (pads IN+/IN-, B+/B-, OUT+/OUT-; DW01 + 8205A chips) | Charges at 1 A as sold (R3 = 1.2 k): fine for a battery of 1000 mAh or more. For a smaller cell, R3 = 2.4 k gives 0.5 A (current = 1200 / R3). The PCB uses the MCP73831 instead | " + amazon('TP4056 charger module protection micro USB') + " |",
         "| 1 | 1S LiPo, about 1100 mAh, JST lead (BT1) | Larger than the PCB's 500 mAh cell (503035), which needs a 0.5 A charger | " + amazon('3.7V 1100mAh LiPo battery JST') + " |",
         "| 1 | JST-PH 2.0 mm 2-pin socket with wire leads (\"pigtail\") | Takes the battery's plug; its two wires go straight into the breadboard. Tin the wire ends (or crimp header pins) so they hold in the holes | " + amazon('JST PH 2.0 2 pin female connector pigtail wire') + " |",
         "| 1 | 1N5817 Schottky diode | Battery -> VSYS (Raspberry Pi's recommended battery hookup) | " + amazon('1N5817 Schottky diode') + " |", "",
         "## Wiring", "",
         "Power rails: Pico pin 36 (3V3) -> **+ rail**, pin 38 (GND) -> **- rail**; the top and bottom rails are "
         "joined at the right-hand end (red and black wires). Use only the rail holes shown: rail holes come in "
         "groups of 5, and on some boards the rails are split in the middle, in which case bridge each rail "
         "across the gap.", "",
         "**Battery:** BT1's red wire goes to the charger's **B+**, black to **B-** (solder them, or use a JST pigtail "
         "with header pins). **The battery's minus never goes on the GND rail**: the module's protection switch "
         "sits between B- and OUT-, and OUT- is the circuit's ground. The battery + reaches the breadboard through "
         f"**OUT+** (column {A_COL}: diode and divider); OUT- goes to the - rail. The module's own micro-USB socket "
         "stays unused when IN+ is fed from the Pico (pin 40); either one charges.", "",
         "| Pico pin | GPIO | Goes to | Signal |", "|---|---|---|---|"]
    for pin, mod, mpin, _, what in WIRES:
        L.append(f"| {pin} | {gpio(pin)} | {NAME[mod]} **{mpin}** | {what} |")
    for i, (pin, b) in enumerate(BUTTONS):
        c = btn_col(i)
        L.append(f"| {pin} | {gpio(pin)} | hole j{c}: {b} button's bottom-left leg (f{c}); "
                 f"the diagonally opposite leg (e{c + 2}) goes to GND (a{c + 2} -> top - rail) | BTN_{b} |")
    L += [f"| 9 | GP6 | nothing (the module has no card-detect pin; the firmware pulls GP6 up inside the Pico) | SD_DET |",
          f"| 37 | 3V3_EN | SW1 middle leg (c{SW1_COLS[1]}); its outer leg c{SW1_COLS[2]} goes to the - rail, c{SW1_COLS[0]} stays free | on/off: knob towards c{SW1_COLS[2]} = OFF |",
          f"| 39 | VSYS | D1 1N5817 **stripe end** (cathode, c{K_COL}). The end WITHOUT the stripe goes in column {A_COL} with the charger's OUT+ and R10. Reversed, the battery cannot power the Pico and USB back-feeds the battery | battery power into the Pico |",
          f"| 34 | GP28 | middle of the R10 + R11 4.7 k divider (column {TAP_COL}: battery + column {A_COL} -> {TAP_COL} -> "
          f"{GND_COL} -> - rail) | VBAT_SENSE (ADC) |", ""]
    L += ["| Module pin | Goes to |", "|---|---|"]
    for mod, mpin, rail in POWER:
        L.append(f"| {NAME[mod]} **{mpin}** | {'+3.3 V rail' if rail == '3V3' else 'GND rail'} |")
    L += [f"| Charger **OUT+** | battery + column {A_COL} (b{A_COL}; D1 anode c{A_COL}, top of R10 d{A_COL}) |",
          "| BT1 red (+) | charger **B+** |",
          "| BT1 black (-) | charger **B-** (never the GND rail) |", "",
          "**On/off switch (SW1):** a 3-leg slide switch, no resistor (the Pico has its own pull-up on 3V3_EN). "
          "Knob towards the GND leg = OFF: the Pico's 3.3 V regulator stops, so the Pico, display, security chip and "
          "+3.3 V rail are off even with USB plugged in, exactly like the PCB's SW1. Knob towards the free leg = ON. "
          "A microSD module powered from pin 40 (5 V, see below) keeps power from USB while the switch is off; that "
          "is harmless.", "",
          "**USB only, no battery (simplest):** leave out BT1, the charger, D1 (the 1N5817 diode: lets the battery "
          "feed VSYS but nothing flow back into it) and R10/R11 (the 4.7 k divider that halves the battery voltage "
          "for GP28; not pull-ups). The Pico runs from its USB cable. The firmware sees that GP28 is not connected "
          "(\"battery: none (GP28 not connected)\" in the log): no battery icon and no \"battery low\" sleep, also "
          "on a wall charger. No transistors and no pull-up resistors are needed anywhere on the breadboard.", "",
          "**Buttons:** no resistors. Each button just connects its Pico pin to GND when pressed; the firmware "
          "turns on the Pico's internal pull-ups (about 50 k) on GP12-GP15, so a released button reads high. (The "
          "PCB also has 10 k pull-ups R26-R29 and RC debounce; the internal pull-ups do no harm there.) The GND "
          "side of every button goes to the **top - rail**, so that rail must be connected to GND (pin 38). "
          "Wire each button by its *diagonally opposite* legs, as shown. A 4-leg tactile button "
          "joins its legs in pairs, and which pairs depends on how it's turned; the diagonal pair is the switch "
          "either way round. (Two legs on the same column can be permanently joined, which would read as "
          "the button held down.)", "",
          "## What the breadboard tests, and what it can't", "",
          "**Tests** (same as the PCB): the display and its SPI wiring, all four buttons, the security chip on I2C, "
          "the microSD card, battery voltage reading, charging from the Pico's USB, running from the "
          "battery, the radio, and all of the firmware.", "",
          "**Does not test** (PCB only):", "",
          "- The power switches Q1-Q5 (SW1, the on/off switch, is on the breadboard). Here the display, security chip and SD card are always powered and the "
          "backlight is always on, so GP7, GP11, GP18 and GP19 switch nothing, and sleep current is higher. "
          "The SE050 has no reset pin (the PCB resets it by switching its power), so to reset it here, pull its 3.3V wire.",
          "- The battery/USB switch Q1: the diode does the same job, with a 0.3 V drop.",
          "- The PCB's USB-C port, fuse and USB protection (here the Pico's own micro-USB is used).",
          "- The charge-status pin GP5 (most charger breakouts only drive an LED).",
          "- **Card detect.** The 6-pin module has no card-detect switch: GP6 just reads high (R20), so the "
          "firmware has to find out whether a card is in by trying to start it. The PCB's socket does have the "
          "switch.", "",
          "## microSD module power", "",
          "VCC goes to the 3.3 V rail, which can't damage any version of the module. Many 6-pin modules are made for "
          "5 V: they carry a 3.3 V regulator (a 3-pin chip, often marked AMS1117-3.3) and a 14-pin level-shifter "
          "chip. Fed 3.3 V, the regulator leaves the card only about 2.2 V, below its 2.7 V minimum, and the card "
          "may not start. If yours has that regulator and the card doesn't start, move its VCC wire to **Pico pin 40 "
          "(VBUS, 5 V)**. Its SPI outputs stay at 3.3 V because its level shifter runs from the regulator. The catch: "
          "pin 40 only has power while USB is plugged in, so on battery alone the SD card is off. Never put 5 V on "
          "a module *without* the regulator: that would destroy the card.", "",
          "How the SD test (`bringup_sd`) shows a power problem: \"no card (nothing answered)\", and on the serial "
          "console \"MISO ... held LOW\" with a raw CMD0 reply of all `00` (the unpowered module drags MISO to "
          "ground), or \"MISO selected: held HIGH\" with all `FF` (module powered, card still silent: under-voltage, "
          "card not seated, or a wrong SCK/MOSI wire). Tested on the breadboard with a 5 V module on pin 40 and a "
          "64 GB exFAT card: no CRC errors at 0.5-8 MHz.", "",
          "## Before powering it", "",
          "1. **3.3 V only.** Never connect VBUS (5 V) to the display, the security chip or the 3.3 V rail.",
          "2. **Check the display's pin order** printed on its back. If it differs from BL CS DC RST SDA SCL VCC GND, "
          "wire by name, not by position.",
          "3. **Check the battery lead polarity** before plugging it into the pigtail: with a meter, the pigtail's "
          "red wire must read + against its black wire. JST-PH leads are not standardised, and reversed ones are common.",
          "4. **With the battery unplugged**, power from USB first and measure: + rail 3.3 V, charger OUT+ about "
          "4.2 V, VSYS (pin 39) about 4.7 V. Then plug the battery in.",
          "5. Use only public test phrases: a breadboard is not a secure device.", ""]
    with open(os.path.join(OUT, "BREADBOARD.md"), "w", encoding="utf8") as fh:
        fh.write("\n".join(L))


if __name__ == "__main__":
    lay = layout()
    problems = check_layout(lay)
    if problems:
        raise SystemExit("breadboard layout check failed:\n  " + "\n  ".join(problems))
    self_test()
    os.makedirs(OUT, exist_ok=True)
    draw(lay).save(os.path.join(OUT, "breadboard.png"))
    markdown(lay)
    print(f"layout check passed ({len(lay.legs)} legs, {len(lay.wires)} wires); "
          "wrote breadboard/breadboard.png and breadboard/BREADBOARD.md")
