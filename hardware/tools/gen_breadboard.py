"""
Breadboard prototype of the tredder board: same Pico pins as the PCB, so the same firmware runs
on both. Writes hardware/breadboard/breadboard.png (the wiring picture) and BREADBOARD.md
(parts, wire-by-wire table, what the breadboard can and cannot test).

Pin numbers come from design.py (TYPES["PICO2W"]) and the GPIO assignment from the same nets the
PCB uses; the wiring table is generated from the WIRES list below, which the picture uses too.

  python tools/gen_breadboard.py
"""
import os

from PIL import Image, ImageDraw, ImageFont

import design as D

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "breadboard")
PICO = dict(D.TYPES["PICO2W"]["left"] + D.TYPES["PICO2W"]["right"])     # pin number -> name

# ------------------------------------------------------------------ what connects to what
# (pico pin, module, module pin, colour, what it is). Pins must match board_pins.h / design.py.
SD, SE, LCD, CHG = "microSD breakout", "Trust M breakout", "Display", "Charger"
WIRES = [
    ("1", SD, "DO", "orange", "SD_MISO (SPI0 RX)"),
    ("2", SD, "CS", "orange", "SD_CS"),
    ("4", SD, "CLK", "orange", "SD_SCK (SPI0 SCK)"),
    ("5", SD, "DI", "orange", "SD_MOSI (SPI0 TX)"),
    ("9", SD, "CD", "white", "SD_DET (card detect)"),
    ("11", SE, "SDA", "blue", "SE_SDA (I2C0 SDA)"),
    ("12", SE, "SCL", "yellow", "SE_SCL (I2C0 SCL)"),
    ("14", SE, "RST", "white", "SE_RST_N (only if the breakout has an RST pin)"),
    ("31", LCD, "SCL", "purple", "LCD_SCK (SPI1 SCK)"),
    ("32", LCD, "SDA", "purple", "LCD_MOSI (SPI1 TX)"),
    ("26", LCD, "RES", "purple", "LCD_RST"),
    ("29", LCD, "DC", "purple", "LCD_DC"),
    ("27", LCD, "CS", "purple", "LCD_CS"),
    ("40", CHG, "VIN", "red", "VBUS: the Pico's USB 5 V feeds the charger"),
]
BUTTONS = [("16", "BACK"), ("17", "CONFIRM"), ("19", "UP"), ("20", "DOWN")]   # GP12, GP13, GP14, GP15
POWER = [  # module pin -> rail
    (SD, "3V", "3V3"), (SD, "GND", "GND"), (SE, "VIN", "3V3"), (SE, "GND", "GND"),
    (LCD, "VCC", "3V3"), (LCD, "GND", "GND"), (LCD, "BLK", "3V3"), (CHG, "GND", "GND"),
]

# ------------------------------------------------------------------ drawing
P = 34                      # breadboard hole pitch, px
BX, BY = 120, 560           # breadboard top-left
COLS = 63
ROWS = "abcdefghij"
COL = {"orange": (245, 140, 30), "white": (235, 235, 235), "blue": (40, 120, 230), "yellow": (240, 210, 40),
       "purple": (160, 80, 220), "red": (220, 40, 40), "black": (25, 25, 25), "green": (40, 170, 70),
       "grey": (150, 150, 150)}
W, H = BX * 2 + COLS * P + 80, 1760
try:
    F = ImageFont.truetype("arialbd.ttf", 19)
    FS = ImageFont.truetype("arial.ttf", 15)
    FT = ImageFont.truetype("arialbd.ttf", 30)
except OSError:
    F = FS = FT = ImageFont.load_default()


def hole(col, row):
    """Centre of breadboard hole (col 1..63, row 'a'..'j' or rail '+t','-t','+b','-b')."""
    x = BX + 40 + (col - 1) * P
    rail = {"+t": BY + 18, "-t": BY + 18 + P, "+b": BY + 18 + P * 2 + 12 * P + 40 + P, "-b": BY + 18 + P * 2 + 12 * P + 40}
    if row in rail:
        return x, rail[row]
    i = ROWS.index(row)
    y = BY + 18 + P * 2 + 30 + i * P + (P if i >= 5 else 0)   # one blank pitch for the centre channel
    return x, y


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
        for r in list(ROWS) + ["+t", "-t", "+b", "-b"]:
            if r[0] in "+-" and c % 6 == 0:
                continue
            x, y = hole(c, r)
            d.rectangle([x - 4, y - 4, x + 4, y + 4], fill=(70, 70, 70))
    for i, r in enumerate(ROWS):
        x, y = hole(1, r)
        d.text((x0 + 6, y - 8), r, fill=(120, 120, 120), font=FS)


PICO_COL = 4                    # pin 1 / pin 40 column


def pico_hole(pin):
    n = int(pin)
    if n <= 20:
        return hole(PICO_COL + n - 1, "c")
    return hole(PICO_COL + (40 - n), "h")


def wire_end(pin):
    """Where a jumper goes in for this Pico pin: row a (top pins) or row j (bottom pins), same column."""
    n = int(pin)
    return hole(PICO_COL + n - 1, "a") if n <= 20 else hole(PICO_COL + (40 - n), "j")


def pico(d):
    x0, y0 = pico_hole("1")
    x1, y1 = pico_hole("21")
    d.rounded_rectangle([x0 - 22, y0 - 20, x1 + 22, y1 + 20], 10, fill=(30, 120, 60), outline=(15, 70, 35), width=3)
    ux = x0 - 22
    d.rectangle([ux - 38, (y0 + y1) / 2 - 26, ux + 10, (y0 + y1) / 2 + 26], fill=(190, 190, 195), outline=(110, 110, 115))
    d.text((x0 + 30, (y0 + y1) / 2 - 30), "Raspberry Pi Pico 2 W", fill=(255, 255, 255), font=FT)
    d.text((x0 + 30, (y0 + y1) / 2 + 8), "USB here (power + programming)", fill=(220, 240, 220), font=FS)
    for pin in [str(i) for i in range(1, 41)]:
        x, y = pico_hole(pin)
        d.ellipse([x - 7, y - 7, x + 7, y + 7], fill=(210, 180, 80), outline=(120, 90, 20))
        name = (PICO[pin].replace("_A0", "").replace("_A1", "").replace("_A2", "").replace("3V3_OUT", "3V3")
                .replace("ADC_VREF", "VREF").replace("3V3_EN", "3V3EN"))
        top = int(pin) <= 20
        vtext(d.im if hasattr(d, "im") else None, d, x, y + (12 if top else -12), f"{pin} {name}", top)


def vtext(_, d, x, y, s, down):
    """Vertical label starting at (x, y), reading downwards (top row) or upwards (bottom row)."""
    w, h = int(d.textlength(s, font=FS)) + 4, 18
    tile = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    ImageDraw.Draw(tile).text((2, 0), s, fill=(255, 255, 255), font=FS)
    tile = tile.rotate(-90 if down else 90, expand=True)
    ox = int(x - tile.width / 2)
    oy = int(y if down else y - tile.height)
    CANVAS[0].paste(tile, (ox, oy), tile)


CANVAS = [None]


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


def route(d, a, b, lane, colour, width=6):
    """Orthogonal wire: from a, vertically to lane y, across, vertically to b."""
    pts = [a, (a[0], lane), (b[0], lane), b]
    d.line(pts, fill=(20, 20, 20), width=width + 3, joint="curve")
    d.line(pts, fill=COL[colour], width=width, joint="curve")
    for p in (a, b):
        d.ellipse([p[0] - 6, p[1] - 6, p[0] + 6, p[1] + 6], fill=COL[colour], outline=(20, 20, 20))


def draw():
    img = Image.new("RGB", (W, H), (250, 250, 247))
    CANVAS[0] = img
    d = ImageDraw.Draw(img)
    d.text((BX, 18), "tredder breadboard prototype: same pins as the PCB (rev A)", fill=(20, 20, 20), font=FT)
    d.text((BX, 58), "Wire colours: orange = microSD SPI, blue/yellow = security chip I2C, purple = display SPI, green = buttons, "
                     "red = +3.3 V / 5 V, black = GND, white = signals.  Full list: BREADBOARD.md",
           fill=(60, 60, 60), font=FS)
    breadboard(d)
    pico(d)

    mods = {
        SD: Module(SD, "microSD breakout (3.3 V)", BX + 30, 150, ["3V", "GND", "CLK", "DO", "DI", "CS", "CD"], "bottom",
                   (60, 90, 140), note="e.g. Adafruit #254"),
        SE: Module(SE, "Infineon Trust M breakout", BX + 640, 150, ["VIN", "GND", "SCL", "SDA", "RST"], "bottom",
                   (110, 60, 140), note="Adafruit #4351 (OPTIGA Trust M)"),
        LCD: Module(LCD, "2.0in ST7789 display (ZJY200S10Z0TG01)", BX + 520, 1490,
                    ["GND", "VCC", "SCL", "SDA", "RES", "DC", "CS", "BLK"], "top", (40, 40, 60), w=760,
                    note="pin order as printed on the module"),
        CHG: Module(CHG, "LiPo charger (MCP73831)", BX + 10, 1490, ["VIN", "GND", "BAT"], "top", (150, 60, 50), w=320,
                    note="e.g. Adafruit Micro-Lipo"),
    }
    for m in mods.values():
        m.draw(d)

    # power rails: top and bottom joined at the right end, fed from 3V3_OUT (36) and GND (38)
    xr = hole(COLS, "+t")[0]
    route(d, hole(COLS - 1, "+t"), hole(COLS - 1, "+b"), (hole(1, "+t")[1] + hole(1, "+b")[1]) // 2, "red", 5)
    route(d, hole(COLS - 2, "-t"), hole(COLS - 2, "-b"), (hole(1, "-t")[1] + hole(1, "-b")[1]) // 2 + 20, "black", 5)
    route(d, wire_end("36"), hole(PICO_COL + 4, "+b"), wire_end("36")[1] + 22, "red", 5)
    route(d, wire_end("38"), hole(PICO_COL + 2, "-b"), wire_end("38")[1] + 10, "black", 5)

    # Pico -> modules
    top_lane, bot_lane = 470, 1430
    for i, (pin, mod, mpin, colour, _) in enumerate(WIRES):
        m = mods[mod]
        a = wire_end(pin)
        b = m.pin_xy(mpin)
        lane = top_lane - i * 12 if int(pin) <= 20 else bot_lane - (i - 8) * 12
        route(d, a, b, lane, colour)
    # module power pins -> nearest rail hole
    for k, (mod, mpin, rail) in enumerate(POWER):
        m = mods[mod]
        b = m.pin_xy(mpin)
        top = m.side == "bottom"
        r = ("+t" if rail == "3V3" else "-t") if top else ("+b" if rail == "3V3" else "-b")
        col = max(1, min(COLS, round((b[0] - BX - 40) / P) + 1))
        while any(abs(hole(col, r)[0] - wire_end(w[0])[0]) < 3 for w in WIRES):
            col += 1
        a = hole(col, r)
        lane = (a[1] + b[1]) // 2 + (k % 3) * 10
        route(d, a, b, lane, "red" if rail == "3V3" else "black")

    # charger -> battery; battery + -> 1N5817 -> VSYS (pin 39); 2 x 4.7k divider battery + -> GP28 (pin 34) -> GND
    chg = mods[CHG]
    bx, by = chg.pin_xy("BAT")
    bat = (BX + 360, 1600)
    d.rounded_rectangle([bat[0], bat[1] - 40, bat[0] + 170, bat[1] + 80], 10, fill=(150, 150, 165), outline=(60, 60, 70), width=3)
    d.text((bat[0] + 12, bat[1] - 30), "LiPo 503035", fill=(20, 20, 20), font=F)
    d.text((bat[0] + 12, bat[1] - 6), "500 mAh, with PCM", fill=(20, 20, 20), font=FS)
    d.text((bat[0] + 12, bat[1] + 14), "JST-PH (check +!)", fill=(150, 20, 20), font=FS)
    d.line([(bx, by), (bx, by - 30), (bat[0] + 20, by - 30), (bat[0] + 20, bat[1] - 40)], fill=COL["red"], width=6)
    A, K, TAP, GNDC = 48, 51, 54, 58          # columns: anode / battery +, cathode, divider tap, divider bottom
    below = hole(1, "+b")[1] + 46
    d.rectangle([hole(A, "j")[0] + 6, hole(A, "j")[1] - 8, hole(K, "j")[0] - 6, hole(K, "j")[1] + 8], fill=(30, 30, 30))
    d.rectangle([hole(K, "j")[0] - 16, hole(K, "j")[1] - 8, hole(K, "j")[0] - 10, hole(K, "j")[1] + 8], fill=(225, 225, 225))
    d.line([hole(A, "j"), (hole(A, "j")[0] + 6, hole(A, "j")[1])], fill=(120, 120, 120), width=3)
    d.line([hole(K, "j"), (hole(K, "j")[0] - 6, hole(K, "j")[1])], fill=(120, 120, 120), width=3)
    d.text((hole(A, "f")[0] - 4, hole(A, "f")[1] - 30), "1N5817", fill=(20, 20, 20), font=F)
    d.text((hole(TAP, "f")[0] - 10, hole(TAP, "f")[1] - 30), "4.7k + 4.7k", fill=(20, 20, 20), font=F)
    resistor(d, hole(A, "g"), hole(TAP, "g"))
    resistor(d, hole(TAP, "h"), hole(GNDC, "h"))
    route(d, (bx, by), hole(A, "i"), below + 30, "red")                       # battery + to the anode column
    route(d, hole(K, "i"), wire_end("39"), below, "red")                     # cathode to VSYS
    route(d, wire_end("34"), hole(TAP, "i"), below + 15, "white")             # divider tap to GP28
    route(d, hole(GNDC, "j"), hole(GNDC, "-b"), hole(1, "-b")[1] - 10, "black", 4)

    # buttons straddling the channel, 10k pull-ups to +3V3, other leg to GND
    for i, (pin, label) in enumerate(BUTTONS):
        c = 30 + i * 4
        top, bot = hole(c, "e"), hole(c, "f")
        d.rectangle([top[0] - 18, top[1] - 6, top[0] + 38, bot[1] + 6], fill=(40, 40, 40), outline=(10, 10, 10), width=2)
        d.ellipse([top[0] - 2, top[1] + 10, top[0] + 22, bot[1] - 10], fill=(200, 200, 200))
        d.text((top[0] - 18, bot[1] + 12), label, fill=(20, 20, 20), font=F)
        route(d, wire_end(pin), hole(c, "a"), wire_end(pin)[1] - 40 - i * 12, "green")
        resistor(d, hole(c + 1, "b"), hole(c + 1, "+t"), vertical=True)
        d.line([hole(c, "b"), hole(c + 1, "b")], fill=COL["grey"], width=4)
        route(d, hole(c, "j"), hole(c, "-b"), hole(c, "-b")[1] - 8, "black", 5)
    return img


def resistor(d, a, b, vertical=False):
    d.line([a, b], fill=(120, 120, 120), width=3)
    mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    if vertical:
        d.rounded_rectangle([mx - 8, my - 18, mx + 8, my + 18], 6, fill=(225, 200, 150), outline=(120, 90, 50))
        d.line([mx - 8, my - 6, mx + 8, my - 6], fill=(140, 80, 20), width=3)
    else:
        d.rounded_rectangle([mx - 18, my - 8, mx + 18, my + 8], 6, fill=(225, 200, 150), outline=(120, 90, 50))
        d.line([mx - 6, my - 8, mx - 6, my + 8], fill=(140, 80, 20), width=3)


def gpio(pin):
    return PICO[pin].split("_")[0]


def markdown():
    L = ["# Breadboard prototype (same pins as the PCB)", "",
         "Generated by `hardware/tools/gen_breadboard.py` (picture: `breadboard.png`). Every signal uses the same "
         "Pico pin as the PCB, so the firmware built for the board runs here unchanged. Pin numbers are the Pico's "
         "physical pins (1-40), with its USB end on the left.", "",
         "![breadboard](breadboard.png)", "",
         "## Parts", "",
         "| Qty | Part | Notes |", "|---|---|---|",
         "| 1 | Raspberry Pi Pico 2 W **with headers** (\"Pico 2 WH\") | Same module as on the PCB |",
         "| 1-2 | Full-size breadboard (830 holes) + jumper wires | |",
         "| 1 | 2.0in 240x320 IPS display, ST7789V, 8-pin **GND VCC SCL SDA RES DC CS BLK** (e.g. ZJY200S10Z0TG01) | The same module the PCB uses |",
         "| 1 | Infineon OPTIGA Trust M breakout with 0.1in pins (Adafruit #4351) | The PCB's U2 is the bare chip; check which Trust M version the breakout carries (the PCB uses V3) |",
         "| 1 | microSD breakout that works at 3.3 V (e.g. Adafruit #254) | |",
         "| 4 | 6 mm tactile push buttons | BACK, CONFIRM, UP, DOWN |",
         "| 4 | 10 k resistors | Button pull-ups (as on the PCB) |",
         "| 2 | 4.7 k resistors | Battery-voltage divider into GP28 (as on the PCB) |",
         "| 1 | LiPo charger breakout with the MCP73831 (e.g. Adafruit Micro-Lipo) | Same charger chip as the PCB |",
         "| 1 | 1S LiPo 503035, 500 mAh, with protection circuit, JST-PH lead | The same cell as the PCB |",
         "| 1 | 1N5817 Schottky diode | Battery -> VSYS (Raspberry Pi's recommended battery hookup) |", "",
         "## Wiring", "",
         "Power rails: Pico pin 36 (3V3) -> **+ rail**, pin 38 (GND) -> **- rail**; join the top and bottom rails at the end.", "",
         "| Pico pin | GPIO | Goes to | Signal |", "|---|---|---|---|"]
    for pin, mod, mpin, _, what in WIRES:
        L.append(f"| {pin} | {gpio(pin)} | {mod} **{mpin}** | {what} |")
    for pin, label in BUTTONS:
        L.append(f"| {pin} | {gpio(pin)} | {label} button (other leg to GND) + 10 k to 3V3 | BTN_{label} |")
    L += ["| 39 | VSYS | 1N5817 cathode (anode to battery +) | battery power into the Pico |",
          "| 34 | GP28 | middle of the 2 x 4.7 k divider (battery + to GND) | VBAT_SENSE (ADC) |", ""]
    L += ["| Module pin | Goes to |", "|---|---|"]
    for mod, mpin, rail in POWER:
        L.append(f"| {mod} **{mpin}** | {'+3.3 V rail' if rail == '3V3' else 'GND rail'} |")
    L += ["| Charger **BAT** | battery + (and the diode's anode, and the top of the divider) |", "",
          "## What the breadboard tests, and what it can't", "",
          "**Tests** (same as the PCB): the display and its SPI wiring, all four buttons, the security chip on I2C, "
          "the microSD card and card detect, battery voltage reading, charging from the Pico's USB, running from the "
          "battery, the radio, and all of the firmware.", "",
          "**Does not test** (PCB only):", "",
          "- The power switches Q1-Q5. Here the display, security chip and SD card are always powered and the "
          "backlight is always on, so GP7, GP11, GP18 and GP19 switch nothing, and sleep current is higher.",
          "- The battery/USB switch Q1: the diode does the same job, with a 0.3 V drop.",
          "- The PCB's USB-C port, fuse and USB protection (here the Pico's own micro-USB is used).",
          "- The charge-status pin GP5 (most charger breakouts only drive an LED).", "",
          "## Before powering it", "",
          "1. **3.3 V only.** Never connect VBUS (5 V) to the display, the security chip or the 3.3 V rail.",
          "2. **Check the display's pin order** printed on its back. If it differs from GND VCC SCL SDA RES DC CS BLK, "
          "wire by name, not by position.",
          "3. **Check the battery lead polarity** before plugging it into the charger: red must go to +.",
          "4. Use only public test phrases: a breadboard is not a secure device.", ""]
    with open(os.path.join(OUT, "BREADBOARD.md"), "w", encoding="utf8") as fh:
        fh.write("\n".join(L))


def check():
    """Every wire must land on the same GPIO the PCB uses for that net."""
    pin_of = {}
    for net, conns in D.NETS.items():
        for c in conns:
            if c.startswith("A1."):
                pin_of[net] = c[3:]
    expect = {"SD_MISO": "1", "SD_CS": "2", "SD_SCK": "4", "SD_MOSI": "5", "SD_DET": "9", "SE_SDA": "11",
              "SE_SCL": "12", "SE_RST_N": "14", "LCD_SCK": "31", "LCD_MOSI": "32", "LCD_RST": "26", "LCD_DC": "29",
              "LCD_CS": "27", "BTN_BACK": "16", "BTN_CONFIRM": "17", "BTN_UP": "19", "BTN_DOWN": "20",
              "VSYS": "39", "VBAT_SENSE": "34", "VBUS": "40"}
    for net, pin in expect.items():
        assert pin_of.get(net) == pin, f"{net}: PCB uses Pico pin {pin_of.get(net)}, breadboard uses {pin}"


if __name__ == "__main__":
    check()
    os.makedirs(OUT, exist_ok=True)
    draw().save(os.path.join(OUT, "breadboard.png"))
    markdown()
    print("wrote breadboard/breadboard.png and breadboard/BREADBOARD.md")
