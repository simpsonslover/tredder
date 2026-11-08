"""
Electrical tests of the breadboard prototype (hardware/breadboard), in ngspice.

The netlist is not typed in by hand: it comes from tools/gen_breadboard.py's layout (the same data
the picture is drawn from), joined into nets by the breadboard strips and wires. So if the picture
misses a wire, these tests see it missing too. Parts reuse the PCB simulation's models
(netlist.py); breadboard-only modules are modelled here (MODEL NOTES).

    "C:\\Program Files\\KiCad\\10.0\\bin\\python.exe" hardware\\sim\\breadboard.py

Writes hardware/breadboard/TEST_REPORT.md. Exit code = number of failing tests.

MODEL NOTES (breadboard-only)
- Charger breakout (Adafruit Micro-Lipo type): MCP73831 (netlist.py model), PROG 10 k = 100 mA
  (the board's default; a solder jumper selects 500 mA), 4.7 uF in and out, STAT LED + 1 k to VIN.
  The battery is wired straight into the breadboard (the breakout's own socket stays empty).
- OM-SE050ARD: the SE050 model from netlist.py, VIN = VCC = ENA from the header 3.3 V; the kit's I2C
  pull-ups J37/J38 at their default 3.3 k (NXP AN12395). The kit's other parts (level shifter
  options, LEDs) are not modelled.
- microSD module (6-pin VCC GND MISO MOSI SCK CS, no card detect): 10 uF on the module and up to
  100 mA while writing (ASSUMED), I/O 10 pF with clamp diodes. A regulator on the module (5 V types)
  is not modelled: the card is assumed to run from VCC directly.
- Jumper wires + breadboard strips: 30 pF per I2C line (ASSUMED: ~25 cm of jumper + strips).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import netlist as N  # noqa: E402  (puts tools/ on sys.path)
import run_all  # noqa: E402
import scenarios as S  # noqa: E402
import gen_breadboard as G  # noqa: E402

LCD_PIN = {name: num for num, name in N.D.TYPES["LCD8"]["left"]}
LCD_PIN.update(RST=LCD_PIN["RES"], BL=LCD_PIN["BLK"])   # the breadboard module's names for the same pins


def bb_board(lay=None):
    """Parts + pin -> net map for the breadboard, from the layout's connectivity."""
    lay = lay or G.layout()
    grp = G.connect(lay)
    want = G.intended()
    name = {}
    for net, ts in want.items():                     # name each piece after the net it was meant to be
        g = max({grp[t] for t in ts}, key=lambda g: sum(grp[t] == g for t in ts))
        name.setdefault(g, net)
    net = lambda t: name.get(grp[t], f"bb_{abs(hash(grp[t])) % 10**6}") if t in grp else None  # noqa: E731
    parts, pin_net = {}, {}

    def part(ref, typ, value="", **pins):
        parts[ref] = dict(type=typ, value=value)
        for p, n in pins.items():
            if n:
                pin_net[f"{ref}.{p.lstrip('_')}"] = n

    for n_ in range(1, 41):
        if net(f"A1.{n_}"):
            pin_net[f"A1.{n_}"] = net(f"A1.{n_}")
    parts["A1"] = dict(type="PICO2W", value="")
    part("BT1", "JST2", _1=net("BAT.+"), _2=net("BAT.-"))
    vin, cg, cb = net("CHG.VIN"), net("CHG.GND"), net("CHG.BAT")
    part("U3", "MCP73831", _4=vin, _2=cg, _3=cb, _5="chg_prog", _1="chg_stat")
    part("RPROG", "R", "10k", _1="chg_prog", _2=cg)
    part("CCHGI", "C", "4.7uF", _1=vin, _2=cg)
    part("CCHGO", "C", "4.7uF", _1=cb, _2=cg)
    part("RSTAT", "R", "1k", _1=vin, _2="chg_led")
    part("LSTAT", "LED", _1="chg_stat", _2="chg_led")
    part("D1", "DIODE", _1=net("D1.A"), _2=net("D1.K"))
    for r, v in [("R10", "4.7k"), ("R11", "4.7k")]:
        part(r, "R", v, _1=net(f"{r}.1"), _2=net(f"{r}.2"))
    for _, b in G.BUTTONS:                           # the diagonal legs are the switch either way round
        part(f"SW_{b}", "BUTTON", _1=net(f"SW_{b}.1"), _2=net(f"SW_{b}.4"))
    v3, sda, scl = net("SE.3.3V"), net("SE.SDA"), net("SE.SCL")
    part("U2", "SE050", _12=v3, _18=v3, _11=v3, _9=sda, _10=scl, _19=net("SE.GND"), _21=net("SE.GND"))
    part("RKSDA", "R", "3.3k", _1=sda, _2=v3)
    part("RKSCL", "R", "3.3k", _1=scl, _2=v3)
    part("LCD", "LCD8", **{f"_{LCD_PIN[p]}": net(f"LCD.{p}") for p in G.MODULE_PINS[G.LCD]})
    part("SD", "SDBREAKOUT", **{f"_{p}": net(f"SD.{p}") for p in G.MODULE_PINS[G.SD]})
    return parts, pin_net


def emit_sd(cfg, ref, n, L):
    c = cfg["card"]
    vdd, gnd = n("VCC"), n("GND")
    L += [f"C{ref}m {vdd} {gnd} 10u",
          f"V{ref}i {ref.lower()}_i 0 {N.pwl(c.get('i', [(0, 0.0005)]) if c.get('inserted', True) else [(0, 0)])}",
          f"B{ref}ld {vdd} {gnd} I=V({ref.lower()}_i)*{N.s(f'V({vdd})', 2.5, 0.05)}"]
    for pin in ("SCK", "MISO", "MOSI", "CS"):
        L += [f"C{ref}p{pin} {n(pin)} 0 10p", f"D{ref}h{pin} {n(pin)} {vdd} DESD", f"D{ref}l{pin} {gnd} {n(pin)} DESD"]


N.EMITTERS["SDBREAKOUT"] = emit_sd


class board:
    """Run the netlist builder on the breadboard instead of the PCB."""

    def __init__(self, lay=None):
        self.lay = lay

    def __enter__(self):
        self.saved = N.PARTS, N.PIN_NET
        N.PARTS, N.PIN_NET = bb_board(self.lay)

    def __exit__(self, *a):
        N.PARTS, N.PIN_NET = self.saved


def cfg(**kw):
    c = N.default_config()
    c.update(battery=dict(v=3.7, cap_mah=500, rint=0.12), card=dict(inserted=True, i=[(0, 0.0005)]))
    c.update(kw)
    return c


def usb(v=5.0):
    return [f"Vusb usb_src 0 {v}", f"Rusb usb_src {N.node('VBUS')} 0.2"]   # Pico micro-USB + cable


def sim(c, lay=None):
    with board(lay):
        return S.spice.run(N.build(c))


V, window = S.V, S.window
Result = S.Result


def end(r, net):
    return float(V(r, net)[-1])


# ------------------------------------------------------------------ tests
def t_bb_layout():
    res = Result("bb_layout", "Breadboard picture: holes, netlist, Pico pins, buttons",
                 "Static check of the layout data the picture is drawn from (gen_breadboard.check_layout): "
                 "every hole exists and holds one leg/wire, strips + wires make exactly the intended netlist, "
                 "each Pico pin has the PCB's net, buttons work either way round. Then the same check on the "
                 "picture with the battery - lead removed must fail (proves the check sees it).")
    lay = G.layout()
    errs = G.check_layout(lay)
    res.check("FAIL", "problems found in the layout", len(errs), "", "0", not errs)
    for e in errs:
        res.notes.append(e)
    res.info("legs / wires checked", f"{len(lay.legs)} / {len(lay.wires)}")
    broken = G.layout()
    broken.wires = [w for w in broken.wires if "BAT.-" not in (w["a"], w["b"])]
    caught = [e for e in G.check_layout(broken) if "BAT.-" in e]
    res.check("FAIL", "self-test: battery - lead removed is reported", len(caught), "problems", ">= 1", caught)
    return res


def t_bb_battery():
    res = Result("bb_battery", "Running from the battery (no USB)",
                 "Battery at 4.2 V (full), 3.7 V and 3.3 V (nearly empty); MCU 60 mA, display + backlight, SE, "
                 "SD idle. VSYS = battery - diode drop; the Pico's buck-boost must hold 3.3 V; GP28 (ADC, pulls off, ADC "
                 "input >= 100 k) must read half the battery voltage. Last row: the same circuit with the battery - lead missing (the "
                 "first version of the picture).")
    for vb in (4.2, 3.7, 3.3):
        r = sim(cfg(battery=dict(v=vb, cap_mah=500, rint=0.12), gpio={"GP28": "z"}, adc_rin=100e3,
                    analysis=".tran 10u 20m"))
        vbat, vsys, v3, sense = end(r, "VBAT"), end(r, "VSYS"), end(r, "+3V3"), end(r, "VBAT_SENSE")
        res.ge(f"[{vb} V] VSYS (RT6154 needs >= 1.8 V)", vsys, 1.8)
        res.between(f"[{vb} V] 3V3 rail", v3, 3.2, 3.4)
        res.between(f"[{vb} V] GP28 / (VBAT/2) (divider accuracy)", sense / (vbat / 2), 0.97, 1.01, "")
        res.info(f"[{vb} V] diode drop VBAT - VSYS", vbat - vsys, "V")
        res.info(f"[{vb} V] battery current", float(r["i(vbat)"][-1]) * 1e3, "mA")
    r = sim(cfg(analysis=".tran 10u 20m"))
    res.info("FIRMWARE: GP28 left with its reset pull-down (36 k worst case): GP28 / (VBAT/2)",
             end(r, "VBAT_SENSE") / (end(r, "VBAT") / 2), "", kind="RULE")
    broken = G.layout()
    broken.wires = [w for w in broken.wires if "BAT.-" not in (w["a"], w["b"])]
    r = sim(cfg(analysis=".tran 10u 20m"), broken)
    res.info("battery - not wired (old picture): 3V3 rail", end(r, "+3V3"), "V", kind="RULE")
    return res


def t_bb_usb():
    res = Result("bb_usb", "USB plugged in: charging and power path",
                 "5 V on the Pico's micro-USB, battery at 3.7 V. The charger runs from VBUS (pin 40) at its "
                 "100 mA setting; the Pico runs from VBUS through its own diode, so D1 must be reverse-biased "
                 "(the battery isn't discharged while charging).")
    r = sim(cfg(extra=usb(), analysis=".tran 10u 20m"))
    ichg = -float(r["i(vbat)"][-1])
    res.between("charge current into the battery", ichg * 1e3, 85, 115, "mA")
    res.le("VBUS (MCP73831 VDD max 6 V; Pico VSYS max 5.5 V)", end(r, "VBUS"), 5.5)
    res.ge("VSYS above the battery (D1 reverse-biased)", end(r, "VSYS") - end(r, "VBAT"), 0.2)
    res.between("3V3 rail", end(r, "+3V3"), 3.2, 3.4)
    res.info("VSYS", end(r, "VSYS"), "V")
    r = sim(cfg(battery=dict(v=4.2, cap_mah=500, rint=0.12), extra=usb(), analysis=".tran 10u 20m"))
    res.le("full battery: charger stops pushing current", -float(r["i(vbat)"][-1]) * 1e3, 10, "mA")
    return res


def t_bb_load():
    res = Result("bb_load", "Worst-case load on a nearly empty battery",
                 "Battery 3.3 V, no USB. MCU 80 mA + radio bursts 60 mA (1 ms every 4 ms), SD card writing "
                 "100 mA, backlight on, SE active (19 mA), all from the Pico's 3V3 pin. Pico datasheet: "
                 "keep 3V3 output <= 300 mA.")
    burst, t = [(0, 0)], 2e-3
    while t < 20e-3:
        burst += [(t, 0), (t + 10e-6, 0.06), (t + 1e-3, 0.06), (t + 1.01e-3, 0)]
        t += 4e-3
    r = sim(cfg(battery=dict(v=3.3, cap_mah=500, rint=0.12), mcu_load=0.08, mcu_burst=burst,
                card=dict(inserted=True, i=[(0, 0.1)]), analysis=".tran 5u 20m"))
    v3, i3 = V(r, "+3V3"), r["i(va1im)"]
    res.ge("3V3 minimum", float(window(r, v3, 2e-3, 20e-3).min()), 3.15)
    res.ge("VSYS minimum", float(window(r, V(r, "VSYS"), 2e-3, 20e-3).min()), 1.8)
    res.le("peak 3V3 output current", float(i3.max()) * 1e3, 300, "mA", kind="WARN")
    res.info("peak battery current", float(r["i(vbat)"].max()) * 1e3, "mA")
    return res


def t_bb_i2c():
    res = Result("bb_i2c", "SE050 kit I2C over jumper wires",
                 "GP8 open-drain toggling at 400 kHz (Fast mode), kit pull-ups 3.3 k, SE050 pins, 30 pF of "
                 "wire per line (assumed). I2C Fast mode: rise time (30 -> 70 %) <= 300 ns, VOL <= 0.4 V.")
    pts = S.pwm(400e3, 0.5, 20e-6, v_on=1, v_off=0, edge=10e-9, t0=2e-6)
    r = sim(cfg(gpio={"GP8": ("od", pts), "GP9": "z"},
                extra=[f"Cwsda {N.node('SE_SDA')} 0 30p", f"Cwscl {N.node('SE_SCL')} 0 30p"],
                analysis=".tran 2n 20u"))
    t, y = r["time"], V(r, "SE_SDA")
    v3 = end(r, "+3V3")
    t30 = S.cross(t, y, 0.3 * v3, True, after=5e-6)
    t70 = S.cross(t, y, 0.7 * v3, True, after=t30 or 5e-6)
    res.le("SDA rise time 30 -> 70 %", ((t70 - t30) if t30 and t70 else 1) * 1e9, 300, "ns")
    res.le("SDA low level", float(window(r, y, 3.0e-6, 3.2e-6).min()), 0.4)
    res.ge("SDA idle high", float(y.max()), 0.7 * v3)
    return res


def t_bb_inputs():
    res = Result("bb_inputs", "Buttons read correctly on internal pull-ups, GP6 held high",
                 "Pico pins as inputs (pulls off). CONFIRM pressed at 5 ms; the other buttons idle. The SD "
                 "module has no card-detect pin, so GP6 (SD_DET) must sit at a defined high level. No resistors on "
                 "the breadboard: GP6 and GP12-GP15 use the RP2350's internal pull-ups (modelled as 32 k, the weak end).")
    r = sim(cfg(gpio={f"GP{g}": "pu" for g in (6, 12, 13, 14, 15)}, buttons={"SW_CONFIRM": [(0, 0), (5e-3, 0), (5.001e-3, 1)]},
                analysis=".tran 10u 10m"))
    for _, b in G.BUTTONS:
        res.ge(f"BTN_{b} released (VIH 2.0 V)", float(window(r, V(r, f"BTN_{b}"), 2e-3, 4.9e-3).min()), 2.0)
    res.le("BTN_CONFIRM pressed (VIL 0.8 V)", end(r, "BTN_CONFIRM"), 0.8)
    with board():
        gp6 = N.pin_node("A1", "9")                  # unconnected on the breadboard: no net name
    res.ge("SD_DET (GP6) held high by the internal pull-up", float(r[f"v({gp6})"][-1]), 2.0)
    res.info("FIRMWARE: the breadboard needs the internal pull-ups on GP6 and GP12-GP15 (no external resistors)",
             "", kind="RULE")
    res.info("no card detect on the breadboard: firmware finds the card by initialising it", "", kind="RULE")
    return res


ALL = [t_bb_layout, t_bb_battery, t_bb_usb, t_bb_load, t_bb_i2c, t_bb_inputs]

if __name__ == "__main__":
    sys.exit(run_all.main(sys.argv[1:], tests=ALL, report=os.path.join(HERE, "..", "breadboard", "TEST_REPORT.md"),
                          title="Breadboard simulation report", source="`sim/breadboard.py` from the layout in "
                          "`tools/gen_breadboard.py`"))
