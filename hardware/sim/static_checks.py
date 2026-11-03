"""
Design checks that don't need SPICE: pin-function legality on the RP2350,
decoupling, power budget, battery life, and KiCad ERC / DRC / parity.
"""
import os
import re
import shutil
import subprocess

import netlist as N
import spice
from scenarios import Result

D = N.D
HW = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
KICAD_CLI = os.path.join(spice.KICAD_BIN, "kicad-cli.exe")

# RP2350 GPIO function mux (datasheet 9.4): role by GPIO number.
def spi(n):
    return f"SPI{(n >> 3) & 1}_" + ("RX", "CSn", "SCK", "TX")[n % 4]


def i2c(n):
    return f"I2C{(n >> 1) & 1}_" + ("SDA", "SCL")[n % 2]


def uart(n):
    return f"UART{(n >> 3) & 1}_" + ("TX", "RX", "CTS", "RTS")[n % 4]


REQUIRED = {  # net -> function it needs from the pin it lands on
    "SD_MISO": "SPI0_RX", "SD_SCK": "SPI0_SCK", "SD_MOSI": "SPI0_TX",
    "LCD_SCK": "SPI1_SCK", "LCD_MOSI": "SPI1_TX",
    "SE_SDA": "I2C0_SDA", "SE_SCL": "I2C0_SCL",
    "VBAT_SENSE": "ADC",
}


def t_pin_functions():
    res = Result("pin_functions", "RP2350 pin-function legality",
                 "Every bus signal must land on a GPIO whose hardware mux offers that function, and the "
                 "SD and display must sit on different SPI blocks.")
    gp_of = {N.net_of_gpio(gp): int(gp[2:]) for gp in N.GPIO_PIN if N.net_of_gpio(gp)}
    for net, fn in REQUIRED.items():
        n = gp_of[net]
        if fn == "ADC":
            ok, have = 26 <= n <= 29, f"GP{n}"
        else:
            have = {"SPI": spi, "I2C": i2c, "UAR": uart}[fn[:3]](n)
            ok = have == fn
        res.check("FAIL", f"{net} on GP{n} -> {have}", have, "", f"== {fn}", ok)
    sd_bus = {spi(gp_of[n])[:4] for n in ("SD_MISO", "SD_SCK", "SD_MOSI")}
    lcd_bus = {spi(gp_of[n])[:4] for n in ("LCD_SCK", "LCD_MOSI")}
    res.check("FAIL", "microSD and display on separate SPI peripherals", f"{sd_bus} / {lcd_bus}", "", "disjoint",
              not (sd_bus & lcd_bus))
    for gp in ("GP23", "GP24", "GP25", "GP29"):
        res.check("FAIL", f"{gp} unused (wired to CYW43 radio inside the module)", gp in N.GPIO_PIN, "", "absent",
                  gp not in N.GPIO_PIN)
    return res


def t_decoupling():
    res = Result("decoupling", "Every supply pin has local decoupling",
                 "Each IC/module supply net must carry at least one capacitor.")
    supplies = {"U2 SE050 VIN/VCC": "SE_VCC", "U3 charger VDD": "VBUS", "U3 charger VBAT": "VBAT",
                "J3 microSD VDD": "SD_VDD", "J4 display VCC": "+3V3", "A1 VSYS": "VSYS", "A1 3V3": "+3V3"}
    for what, net in supplies.items():
        caps = [rp.split(".")[0] for rp in D.NETS[net] if D.P[rp.split(".")[0]]["type"] == "C"]
        res.check("FAIL", f"{what} ({net})", ", ".join(caps) or "none", "", ">= 1 capacitor", bool(caps))
    return res


def t_power_budget():
    res = Result("power_budget", "3V3 power budget and battery life",
                 "Worst-case simultaneous loads on the Pico's 3V3 output (Raspberry Pi recommends <= 300 mA), "
                 "and battery life estimates for a 1000 mAh cell.")
    loads = {"RP2350 + flash, both cores busy": 45, "CYW43439 BLE TX peak": 60, "Display logic": 8,
             "Backlight (full)": 30, "microSD write peak": 100, "SE050 active (AES + ECC)": 19, "Pull-ups, dividers": 2}
    total = sum(loads.values())
    for k, v in loads.items():
        res.info(k, v, "mA")
    res.le("Total worst-case 3V3 load", total, 300, "mA")
    eff, vbat = 0.88, 3.7
    for label, i3 in (("Active (screen on, BLE connected)", 75), ("Idle, screen off, BLE advertising", 12)):
        ibat = i3 * 3.3 / (eff * vbat)
        res.info(f"{label}: battery current / runtime on 1000 mAh", f"{ibat:.0f} mA / {1000 / ibat:.1f} h", "")
    return res


def _cli():
    path = KICAD_CLI if os.path.exists(KICAD_CLI) else shutil.which("kicad-cli")
    return path


def t_kicad():
    res = Result("kicad_checks", "KiCad ERC, DRC and schematic/PCB parity",
                 "Runs kicad-cli on the real project files.")
    cli = _cli()
    if not cli:
        res.check("WARN", "kicad-cli not found", "-", "", "present", False)
        return res
    out = os.path.join(N.HERE, "out")
    os.makedirs(out, exist_ok=True)
    erc = os.path.join(out, "erc.rpt")
    subprocess.run([cli, "sch", "erc", "--severity-all", "-o", erc, os.path.join(HW, "wallet.kicad_sch")],
                   capture_output=True)
    txt = open(erc, encoding="utf8", errors="replace").read()
    m = re.search(r"ERC messages: (\d+)\s+Errors (\d+)\s+Warnings (\d+)", txt)
    res.check("FAIL", "ERC errors", int(m.group(2)) if m else -1, "", "== 0", m and m.group(2) == "0")
    res.check("WARN", "ERC warnings", int(m.group(3)) if m else -1, "", "== 0", m and m.group(3) == "0")
    drc = os.path.join(out, "drc.rpt")
    p = subprocess.run([cli, "pcb", "drc", "--refill-zones", "--schematic-parity", "--severity-error", "-o", drc,
                        os.path.join(HW, "wallet.kicad_pcb")], capture_output=True, text=True)
    txt = open(drc, encoding="utf8", errors="replace").read()
    unconn = len(re.findall(r"^\[unconnected_items\]", txt, re.M))
    parity = re.search(r"Found (\d+) schematic parity", p.stdout)
    kinds = re.findall(r"^\[([a-z_]+)\]", txt, re.M)
    copper = [k for k in kinds if k in ("clearance", "shorting_items", "tracks_crossing", "hole_clearance",
                                         "copper_edge_clearance", "via_dangling", "track_dangling")]
    res.check("FAIL", "Unrouted connections", unconn, "", "== 0", unconn == 0)
    res.check("FAIL", "Copper DRC errors (clearance/short/crossing/edge)", len(copper), "", "== 0", not copper)
    res.check("FAIL", "Schematic <-> PCB parity issues", int(parity.group(1)) if parity else -1, "", "== 0",
              bool(parity) and parity.group(1) == "0")
    other = sorted(set(kinds) - set(copper) - {"unconnected_items"})
    res.info("Other DRC error classes (see README: known/benign)", ", ".join(other) or "none")
    return res


ALL = [t_pin_functions, t_decoupling, t_power_budget, t_kicad]
