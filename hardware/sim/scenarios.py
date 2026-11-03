"""
Circuit-level test scenarios for the wallet board (SPICE via ngspice).

Every scenario builds the full board netlist from design.py, adds a stimulus
(USB, battery, power switch, firmware GPIO behaviour, buttons), simulates it,
and grades the waveforms against datasheet / spec limits.

Check kinds
  FAIL  hard limit (datasheet absolute max, spec window, design requirement)
  WARN  margin concern / recommended operating range
  RULE  requirement the Phase 2 firmware must follow (shown by simulation)
  INFO  measured value for the record
"""
import os

import numpy as np

import netlist as N
import plot
import spice

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

# Datasheet limits (see netlist.py MODEL NOTES for sources)
RP_VIH, RP_VIL = 2.0, 0.8
RP_FT_POWERED, RP_FT_UNPOWERED = 5.5, 3.63
VBUS_ABS = 6.0           # Pico VSYS / RT6154 input: keep below 6 V (datasheet VSYS max 5.5 V recommended)
MCP_VDD_ABS = 7.0
SE_VCC_MAX = 3.6       # SE050 VIN/VCC operating maximum (datasheet Table 9; abs max 6 V)
SD_VDD_MAX = 3.6


class Result:
    def __init__(self, name, title, what):
        self.name, self.title, self.what = name, title, what
        self.checks, self.plots, self.notes = [], [], []
        self.error = None

    def check(self, kind, desc, val, unit, limit, ok):
        self.checks.append(dict(kind=kind, desc=desc, val=val, unit=unit, limit=limit,
                                ok=bool(ok) if kind in ("FAIL", "WARN") else None))

    def le(self, desc, val, lim, unit="V", kind="FAIL"):
        self.check(kind, desc, val, unit, f"<= {lim:g} {unit}", val <= lim)

    def ge(self, desc, val, lim, unit="V", kind="FAIL"):
        self.check(kind, desc, val, unit, f">= {lim:g} {unit}", val >= lim)

    def between(self, desc, val, lo, hi, unit="V", kind="FAIL"):
        self.check(kind, desc, val, unit, f"{lo:g} .. {hi:g} {unit}", lo <= val <= hi)

    def info(self, desc, val, unit="", kind="INFO"):
        self.check(kind, desc, val, unit, "", True)

    @property
    def failed(self):
        return self.error is not None or any(c["kind"] == "FAIL" and not c["ok"] for c in self.checks)

    @property
    def warned(self):
        return any(c["kind"] == "WARN" and not c["ok"] for c in self.checks)


def cfg(**kw):
    c = N.default_config()
    c.update(kw)
    return c


def sim(c):
    return spice.run(N.build(c))


def V(r, net):
    if net == "GND":
        return np.zeros_like(r["time"]) if "time" in r else np.zeros(1)
    return r[f"v({N.node(net)})"]


def Vp(r, ref, pin):
    return r[f"v({N.pin_node(ref, pin)})"]


def at(r, t, y):
    return float(np.interp(t, r["time"], y))


def window(r, y, t0, t1):
    t = r["time"]
    m = (t >= t0) & (t <= t1)
    return y[m] if m.any() else y[np.argmin(abs(t - t0)):][:1]


def cross(t, y, level, rising=True, after=0.0):
    m = t >= after
    t, y = t[m], y[m]
    idx = np.where((y[:-1] < level) & (y[1:] >= level))[0] if rising else \
        np.where((y[:-1] > level) & (y[1:] <= level))[0]
    if not len(idx):
        return None
    i = idx[0]
    return float(t[i] + (level - y[i]) * (t[i + 1] - t[i]) / (y[i + 1] - y[i]))


def pwm(f, duty, t_end, v_on=0.0, v_off=3.3, edge=20e-9, t0=0.0):
    pts, T, t = [(0, v_off)], 1 / f, t0
    while t < t_end:
        pts += [(t, v_off), (t + edge, v_on), (t + duty * T, v_on), (t + duty * T + edge, v_off)]
        t += T
    return pts


# ---------------------------------------------------------------- global invariants

def invariants(res, r, c):
    """Absolute-max checks applied to every transient simulation."""
    if "time" not in r:
        return
    v3 = V(r, "+3V3")
    worst_ft, worst_adc = (None, -1e9), (None, -1e9)
    for gp, num in N.GPIO_PIN.items():
        vp = r[f"v({N.pin_node(N.PICO, num)})"]
        if gp in N.ADC_GPIOS:
            margin = float(np.max(vp - (v3 + 0.5)))
            if margin > worst_adc[1]:
                worst_adc = (gp, margin)
        else:
            lim = np.where(v3 > 3.0, RP_FT_POWERED, RP_FT_UNPOWERED)
            margin = float(np.max(vp - lim))
            if margin > worst_ft[1]:
                worst_ft = (gp, margin)
    res.le(f"[abs max] RP2350 FT pin margin vs 5.5 V powered / 3.63 V unpowered (worst: {worst_ft[0]} "
           f"{N.net_of_gpio(worst_ft[0])})", worst_ft[1], 0.0)
    res.le(f"[abs max] RP2350 ADC pin above IOVDD+0.5 (worst: {worst_adc[0]})", worst_adc[1], 0.0)
    res.le("[max operating] SE050 VIN/VCC", float(V(r, "SE_VCC").max()), SE_VCC_MAX)
    res.le("[abs max] microSD VDD", float(V(r, "SD_VDD").max()), SD_VDD_MAX)
    res.le("[abs max] MCP73831 VDD (VBUS)", float(V(r, "VBUS").max()), MCP_VDD_ABS)


def save_plot(res, r, panels, title=None, xscale=1.0, xlabel="time (s)"):
    os.makedirs(OUT, exist_ok=True)
    fn = f"{res.name}{'' if not res.plots else '_' + str(len(res.plots) + 1)}.svg"
    plot.chart(os.path.join(OUT, fn), title or res.title, r["time"] * xscale, panels, xlabel=xlabel)
    res.plots.append(fn)


# ---------------------------------------------------------------- scenarios

def t_reset_default_off():
    res = Result("reset_default_off", "Boot / reset: peripherals default OFF",
                 "All GPIOs in RP2350 reset state (input, pull-DOWN 36-113 k). The SE, microSD, "
                 "display power and backlight switches must stay OFF until firmware turns them on. Swept over pull-down "
                 "and AO3401A Vth corners.")
    rows = []
    for rpd in (36e3, 113e3):
        for vth in ("min", "typ", "max"):
            r = sim(cfg(rpd=rpd, vth=vth, analysis=".op"))
            se, sd = float(V(r, "SE_VCC")[0]), float(V(r, "SD_VDD")[0])
            lcd = float(V(r, "LCD_VCC")[0])
            bl = float(r["i(vj4blm)"][0])
            tag = f"RPD {rpd / 1e3:.0f}k, Vth {N.VTH[vth]:+.1f} V"
            res.le(f"SE_VCC at reset ({tag})", se, 0.3)
            res.le(f"SD_VDD at reset ({tag})", sd, 0.3)
            res.le(f"LCD_VCC at reset ({tag})", lcd, 0.3)
            res.le(f"Backlight current at reset ({tag})", bl * 1e3, 1.0, "mA")
            rows.append((tag, se, sd, bl))
            for b in ("BTN_UP", "BTN_DOWN", "BTN_BACK", "BTN_CONFIRM"):
                if rpd == 36e3 and vth == "typ":
                    res.ge(f"{b} idle level with reset pull-down (reads 'not pressed')",
                           float(V(r, b)[0]), RP_VIH, kind="WARN")
    return res


def t_battery_boot():
    res = Result("battery_boot", "Power-on from battery (power switch flipped ON)",
                 "Battery 3.7 V and 3.4 V, no USB, power switch turned ON at 1 ms.")
    for vb in (3.7, 3.4):
        r = sim(cfg(battery=dict(v=vb), sw1=[(0, 0), (1e-3, 0), (1.01e-3, 1)], analysis=".tran 5u 12m"))
        t, v3 = r["time"], V(r, "+3V3")
        t_up = cross(t, v3, 3.2)
        res.le(f"[VBAT {vb} V] 3V3 reaches 3.2 V after switch-on", (t_up - 1e-3) * 1e3 if t_up else 99, 5, "ms")
        res.between(f"[VBAT {vb} V] 3V3 steady state", float(v3[-1]), 3.2, 3.4)
        res.le(f"[VBAT {vb} V] 3V3 overshoot", float(v3.max()), 3.6)
        res.info(f"[VBAT {vb} V] battery current, idle (MCU 60 mA)", float(r["i(vbat)"][-1]) * 1e3, "mA")
        res.le(f"[VBAT {vb} V] SE_VCC glitch during power-up (GPIOs in reset)", float(V(r, "SE_VCC").max()), 0.3)
        res.le(f"[VBAT {vb} V] SD_VDD glitch during power-up (GPIOs in reset)", float(V(r, "SD_VDD").max()), 0.3)
        res.le(f"[VBAT {vb} V] backlight current during power-up", float(r["i(vj4blm)"].max()) * 1e3, 1.0, "mA")
        invariants(res, r, None)
        if vb == 3.7:
            save_plot(res, r, [dict(ylabel="V", series=[("VBAT", V(r, "VBAT")), ("VSYS", V(r, "VSYS")),
                                                       ("3V3", v3), ("3V3_EN", V(r, "3V3_EN"))])],
                      xscale=1e3, xlabel="time (ms)")
    return res


def t_usb_hotplug():
    res = Result("usb_hotplug", "USB-C hot-plug and unplug with battery (load sharing)",
                 "Battery 3.7 V, switch ON, MCU running. A live 5 V charger is hot-plugged through a cable of "
                 "1 uH / 0.1 Ohm (worst-case ringing) and later unplugged. Ringing is simulated with a 5 ns "
                 "step for both ends of the PTC fuse range (0.15 / 0.3 Ohm).")
    for fr in (0.15, 0.3):
        c = cfg(battery=dict(v=3.7), fuse_r=fr, analysis=".tran 5n 1.5m 0 20n",
                usb=dict(v=5.0, L=1e-6, R=0.1, plug=[(0, 0), (1e-3, 0), (1.0001e-3, 1)]))
        r = sim(c)
        res.le(f"[PTC {fr} Ohm] peak VBUS_C at USB-C connector (before F1)", float(V(r, "VBUS_C").max()), VBUS_ABS)
        res.le(f"[PTC {fr} Ohm] peak VBUS on board (Pico VBUS/VSYS, charger)", float(V(r, "VBUS").max()), 5.5, kind="WARN")
        res.le(f"[PTC {fr} Ohm] peak VBUS on board (abs max)", float(V(r, "VBUS").max()), VBUS_ABS)
        invariants(res, r, c)
        if fr == 0.15:
            save_plot(res, r, [dict(ylabel="V", series=[("VBUS_C (connector)", V(r, "VBUS_C")), ("VBUS (after F1)", V(r, "VBUS")),
                                                       ("VSYS", V(r, "VSYS"))], limits=[("6.0 V", VBUS_ABS), ("5.5 V", 5.5)])],
                      title="USB hot-plug ringing, PTC 0.15 Ohm", xscale=1e3, xlabel="time (ms)")
    c = cfg(battery=dict(v=3.7), usb=dict(v=5.0, L=1e-6, R=0.1, plug=[(0, 0), (2e-3, 0), (2.001e-3, 1),
                                                                   (40e-3, 1), (40.001e-3, 0)]),
            analysis=".tran 2u 300m 0 20u")
    r = sim(c)
    t, vbus, vsys, v3 = r["time"], V(r, "VBUS"), V(r, "VSYS"), V(r, "+3V3")
    ibat = -r["i(vbat)"]
    res.ge("3V3 minimum across plug + unplug", float(v3[t > 1e-3].min()), 3.2)
    res.ge("VSYS minimum after unplug (RT6154 needs >= 1.8 V)", float(window(r, vsys, 40e-3, 0.3).min()), 2.5)
    chg = window(r, ibat, 25e-3, 39e-3)
    res.between("Battery charge current while USB present (system runs from USB)", float(chg.mean()), 0.45, 0.55, "A")
    q1_vgs = window(r, vbus - vsys, 10e-3, 39e-3)
    res.ge("Q1 Vgs while USB present (FET off: gate above source)", float(q1_vgs.min()), 0.0)
    diode = (vsys < V(r, "VBAT") - 0.3) & (t > 40e-3)
    res.info("Time Q1 body diode carries the load after unplug (VSYS < VBAT - 0.3 V)",
             float(np.ptp(t[diode])) * 1e3 if diode.any() else 0.0, "ms")
    invariants(res, r, c)
    save_plot(res, r, [dict(ylabel="V", series=[("VBUS", vbus), ("VSYS", vsys), ("VBAT", V(r, "VBAT")), ("3V3", v3)]),
                       dict(ylabel="A", series=[("battery charge (+) / discharge (-)", ibat)])],
              title="USB plug at 2 ms, unplug at 40 ms", xscale=1e3, xlabel="time (ms)")
    return res


def t_switch_off_usb():
    res = Result("switch_off_usb", "Power switch OFF while charging",
                 "SW1 OFF (battery disconnected from system, 3V3_EN grounded) with USB attached. "
                 "The device must be completely off but still charge. Worst case: VBUS 5.5 V and a full "
                 "battery (charger STAT drives VBUS-level 'complete' into the divider).")
    for vbus, soc in ((5.0, 0.5), (5.5, 1.0)):
        c = cfg(battery=dict(soc=soc), sw1=[(0, 0)], usb=dict(v=vbus, L=0.3e-6), analysis=".tran 10u 20m")
        r = sim(c)
        tag = f"VBUS {vbus} V, battery {'full' if soc == 1 else 'half'}"
        res.le(f"[{tag}] 3V3 (must stay off)", float(V(r, "+3V3")[-1]), 0.3)
        ibat = float(-r["i(vbat)"][-1])
        if soc < 1:
            res.between(f"[{tag}] charge current with switch OFF", ibat, 0.45, 0.55, "A")
        stat = float(V(r, "CHG_STAT").max())
        res.le(f"[{tag}] CHG_STAT into UNPOWERED RP2350 GP5 (FT limit 3.63 V)", stat, RP_FT_UNPOWERED)
        invariants(res, r, c)
    return res


def t_off_drain():
    res = Result("off_drain", "Battery drain when switched OFF / idle",
                 "No USB. SW1 OFF: battery must see only charger leakage. SW1 ON with firmware asleep is "
                 "reported for reference (model MCU load 1 mA).")
    r = sim(cfg(battery=dict(v=3.8), sw1=[(0, 0)], analysis=".op"))
    i_off = float(r["i(vbat)"][0])
    res.le("Battery current, switch OFF", i_off * 1e6, 5.0, "uA")
    res.info("Shelf life of 1000 mAh cell from this drain alone",
             1000e-3 / max(i_off, 1e-9) / 24 / 365, "years")
    off = {"GP28": "z", "GP7": "high", "GP11": "high", "GP19": "high", "GP18": "high"}
    lcd_pins_low = {g: "low" for g in ("GP20", "GP21", "GP22", "GP26", "GP27")}
    r = sim(cfg(battery=dict(v=3.8), mcu_load=1e-3, analysis=".op", gpio={**off, **lcd_pins_low}))
    i_sleep = float(r["i(vbat)"][0])
    res.le("Battery current in SLEEP (switch ON, MCU 1 mA dormant, display + SE + SD unpowered)", i_sleep * 1e3, 3.0, "mA")
    res.info("Sleep time of a 500 mAh cell (BT1) from this drain", 0.5 / i_sleep / 24, "days")
    r = sim(cfg(battery=dict(v=3.8), mcu_load=1e-3, analysis=".op", gpio={**off, "GP18": "low"}))
    res.info("Same, display module left powered (no Q5 / firmware forgets): module logic load",
             float(r["i(vbat)"][0]) * 1e3, "mA")
    return res


def t_lcd_power():
    res = Result("lcd_power", "Display module power switch (Q5): sleep / wake",
                 "Firmware: GP18 low at 2 ms (display on), LCD pins driven after 5 ms, pins LOW at 29 ms, "
                 "GP18 high at 30 ms (sleep). Second run leaves CS/DC high after power-off (firmware mistake).")
    on = [(0, 3.3), (2e-3, 3.3), (2.0001e-3, 0), (30e-3, 0), (30.0001e-3, 3.3)]
    pin = [(0, 0), (5e-3, 0), (5.0001e-3, 3.3), (29e-3, 3.3), (29.0001e-3, 0)]
    base = dict(battery=dict(v=3.7), analysis=".tran 5u 200m 0 50u")
    c = cfg(gpio={"GP18": ("pwl", on), "GP21": ("pwl", pin), "GP22": ("pwl", pin), "GP20": ("pwl", pin),
                  "GP26": "low", "GP27": "low", "GP19": "high"}, **base)
    r = sim(c)
    t, lv, v3 = r["time"], V(r, "LCD_VCC"), V(r, "+3V3")
    t10, t90 = cross(t, lv, 0.33, after=2e-3), cross(t, lv, 2.97, after=2e-3)
    res.le("LCD_VCC reaches 90 % (then the driver may send SWRESET)", (t90 - 2e-3) * 1e3 if t90 else 99, 3.0, "ms")
    res.info("LCD_VCC ramp 10-90 %", (t90 - t10) * 1e3 if t10 and t90 else 99, "ms")
    res.ge("3V3 minimum at display switch-on (rail stays in spec)", float(window(r, v3, 2e-3, 10e-3).min()), 3.2)
    res.info("3V3 droop at display switch-on", (3.3 - float(window(r, v3, 2e-3, 10e-3).min())) * 1e3, "mV")
    toff = cross(t, lv, 0.3, rising=False, after=30e-3)
    res.info("LCD_VCC below 0.3 V after power-off", (toff - 30e-3) * 1e3 if toff else 999, "ms", kind="RULE")
    invariants(res, r, c)
    save_plot(res, r, [dict(ylabel="V", series=[("LCD_VCC", lv), ("3V3", v3), ("LCD_GATE", V(r, "LCD_GATE"))])],
              xscale=1e3, xlabel="time (ms)")
    r2 = sim(cfg(gpio={"GP18": ("pwl", on), "GP21": ("pwl", [(0, 0), (5e-3, 3.3)]), "GP22": ("pwl", [(0, 0), (5e-3, 3.3)]),
                       "GP20": "low", "GP26": "low", "GP27": "low", "GP19": "high"},
                 **dict(base, analysis=".tran 5u 100m 0 50u")))
    res.info("FIRMWARE MISTAKE: LCD_VCC with display 'off' but CS/DC left high (back-powered through ESD diodes)",
             float(V(r2, "LCD_VCC")[-1]), "V", kind="RULE")
    return res


def t_charge_cycle():
    res = Result("charge_cycle", "Full charge cycle (CC -> CV -> complete)",
                 "Battery capacity scaled to 2 mAh so a full CC/CV cycle fits in 40 s of simulated time "
                 "(currents and voltages are those of the real circuit). Firmware reads GP5 as input.")
    c = cfg(battery=dict(soc=0.02, cap_mah=2, rint=0.12), usb=dict(v=5.0, L=0.3e-6),
            gpio={"GP5": "z"}, analysis=".tran 5m 40 0 20m")
    r = sim(c)
    t, vb, ib = r["time"], V(r, "VBAT"), -r["i(vbat)"]
    stat = V(r, "CHG_STAT")
    led = (V(r, "VBUS") - V(r, "CHG_LED")) / N.value(N.D.P["R6"]["value"])
    cc = ib > 0.4
    res.between("Constant-current phase current (R5 sets 1000/R5)", float(np.median(ib[cc])), 0.45, 0.55, "A")
    res.le("Maximum VBAT (CV regulation 4.20 V)", float(vb.max()), 4.25)
    res.le("CHG_STAT level while charging (must read LOW)", float(stat[cc].max()), RP_VIL)
    res.ge("CHG_STAT level when complete (must read HIGH)", float(stat[-1]), RP_VIH)
    res.between("Charge LED current while charging", float(np.median(led[cc])) * 1e3, 1.0, 6.0, "mA")
    tdone = cross(t, stat, RP_VIH)
    res.info("Time to 'complete' (2 mAh scaled cell)", tdone or -1, "s")
    invariants(res, r, c)
    save_plot(res, r, [dict(ylabel="V", series=[("VBAT", vb), ("CHG_STAT (GP5)", stat)], limits=[("4.2 V", 4.2)]),
                       dict(ylabel="A", series=[("charge current", ib)])])
    return res


def t_sd_power():
    res = Result("sd_power", "microSD power switch: soft-start, write load, power-off",
                 "Firmware: GP7 low at 2 ms (SD on), card writes 60 mA 10-20 ms, GP7 high at 30 ms (off). "
                 "SPI lines driven low after power-off (correct firmware). A second run leaves CS high after "
                 "power-off (firmware mistake) to show back-powering.")
    on = [(0, 3.3), (2e-3, 3.3), (2.0001e-3, 0), (30e-3, 0), (30.0001e-3, 3.3)]
    hi_then_low = [(0, 0), (2e-3, 0), (2.5e-3, 3.3), (29.9e-3, 3.3), (29.9001e-3, 0)]
    card = [(0, 0.0005), (10e-3, 0.0005), (10.01e-3, 0.06), (20e-3, 0.06), (20.01e-3, 0.0005)]
    base = dict(battery=dict(v=3.7), card=dict(inserted=True, i=card), analysis=".tran 5u 400m 0 50u")
    c = cfg(gpio={"GP7": ("pwl", on), "GP1": ("pwl", hi_then_low), "GP2": "low", "GP3": "low", "GP0": "z"}, **base)
    r = sim(c)
    t, sd, v3 = r["time"], V(r, "SD_VDD"), V(r, "+3V3")
    ireg = r["i(va1im)"]
    t10, t90 = cross(t, sd, 0.33, after=2e-3), cross(t, sd, 2.97, after=2e-3)
    rise = (t90 - t10) * 1e3 if t10 and t90 else 99
    res.between("SD_VDD ramp 10-90 % (SD spec: 0.1 .. 35 ms)", rise, 0.1, 35, "ms")
    res.ge("SD_VDD while on, during 60 mA write", float(window(r, sd, 10e-3, 20e-3).min()), 3.2)
    base_i = at(r, 1.9e-3, ireg)
    res.ge("3V3 minimum at SD switch-on (rail stays in spec)", float(window(r, v3, 2e-3, 10e-3).min()), 3.2)
    res.info("Inrush from 3V3 rail above baseline", float((window(r, ireg, 2e-3, 10e-3) - base_i).max()) * 1e3, "mA")
    res.info("3V3 droop at SD switch-on", (3.3 - float(window(r, v3, 2e-3, 10e-3).min())) * 1e3, "mV")
    t_off = cross(t, sd, 0.5, rising=False, after=30e-3)
    res.info("SD_VDD fall below 0.5 V after power-off (card reset needs < 0.5 V for >= 1 ms)",
             (t_off - 30e-3) * 1e3 if t_off else 999, "ms", kind="RULE")
    invariants(res, r, c)
    save_plot(res, r, [dict(ylabel="V", series=[("SD_VDD", sd), ("3V3", v3), ("SD_GATE", V(r, "SD_GATE"))]),
                       dict(ylabel="A", series=[("3V3 regulator output", ireg)])], xscale=1e3, xlabel="time (ms)")
    c2 = cfg(gpio={"GP7": ("pwl", on), "GP1": ("pwl", [(0, 0), (2.5e-3, 3.3)]), "GP2": "low", "GP3": "low",
                   "GP0": "z"}, **dict(base, analysis=".tran 5u 100m 0 50u"))
    r2 = sim(c2)
    res.info("FIRMWARE MISTAKE: SD_VDD with card 'off' but CS left high (back-powered through card I/O)",
             float(V(r2, "SD_VDD")[-1]), "V", kind="RULE")
    return res


def t_se_power():
    res = Result("se_power", "Secure element power-up / hibernate",
                 "GP11 low at 1 ms (SE on), high at 21 ms (off: the SE050 has no reset pin, so this "
                 "power cycle is its reset). Second run: firmware drives SDA (GP8) push-pull high while the SE is off.")
    on = [(0, 3.3), (1e-3, 3.3), (1.0001e-3, 0), (21e-3, 0), (21.0001e-3, 3.3)]
    c = cfg(battery=dict(v=3.7), gpio={"GP11": ("pwl", on)}, analysis=".tran 2u 300m 0 50u")
    r = sim(c)
    t, se, v3 = r["time"], V(r, "SE_VCC"), V(r, "+3V3")
    tup = cross(t, se, 3.2, after=1e-3)
    res.le("SE_VCC reaches 3.2 V after enable", (tup - 1e-3) * 1e3 if tup else 99, 1.0, "ms")
    res.ge("3V3 minimum at SE switch-on (rail stays in spec)", float(window(r, v3, 1e-3, 3e-3).min()), 3.2)
    res.info("3V3 droop at SE switch-on", (3.3 - float(window(r, v3, 1e-3, 3e-3).min())) * 1e3, "mV")
    toff = cross(t, se, 0.3, rising=False, after=21e-3)
    res.info("SE_VCC below 0.3 V after power-off (hibernate)", (toff - 21e-3) * 1e3 if toff else 999, "ms", kind="RULE")
    invariants(res, r, c)
    save_plot(res, r, [dict(ylabel="V", series=[("SE_VCC", se), ("SE_SDA", V(r, "SE_SDA")),
                                               ("SE_PWR_N (GP11)", V(r, "SE_PWR_N"))])], xscale=1e3, xlabel="time (ms)")
    res.le("SE_SDA idle level with the SE off (pull-ups follow the SE supply)",
           float(V(r, "SE_SDA")[-1]), 0.3)
    r2 = sim(cfg(battery=dict(v=3.7), gpio={"GP11": ("pwl", on), "GP8": "high"}, analysis=".tran 2u 300m 0 50u"))
    res.info("FIRMWARE MISTAKE: SE_VCC 'off' with SDA driven high (fed through the 4.7k pull-up and the "
             "SE's weak pull-up)", float(V(r2, "SE_VCC")[-1]), "V", kind="RULE")
    return res


def t_i2c():
    res = Result("i2c_timing", "I2C edge rates to the secure element",
                 "SE powered, MCU pulls SDA (open-drain) at 400 kHz. Bus = MCU pin 5 pF + SE 10 pF + "
                 "trace 15 pF (assumed). I2C spec rise time (30->70 %): Fm <= 300 ns, Fm+ <= 120 ns.")
    pts, t, half = [(0, 0)], 2e-6, 1.25e-6
    while t < 12e-6:
        pts += [(t, 0), (t + 5e-9, 1), (t + half, 1), (t + half + 5e-9, 0)]
        t += 2 * half
    c = cfg(battery=dict(v=3.7), gpio={"GP11": "low", "GP8": ("od", pts), "GP9": "z"},
            extra=[f"Ctrace {N.node('SE_SDA')} 0 15p"], analysis=".tran 1n 12u")
    r = sim(c)
    tt, sda = r["time"], V(r, "SE_SDA")
    vdd = 3.3
    rises = []
    for k in range(4):
        a = cross(tt, sda, 0.3 * vdd, after=2e-6 + k * 2.5e-6 + half)
        b = cross(tt, sda, 0.7 * vdd, after=a or 0) if a else None
        if a and b:
            rises.append(b - a)
    tr = max(rises) * 1e9 if rises else 9999
    res.le("SDA rise time 30->70 % (Fast-mode 400 kHz)", tr, 300, "ns")
    res.le("SDA rise time 30->70 % (Fast-mode Plus 1 MHz)", tr, 120, "ns", kind="WARN")
    res.le("SDA low level (VOL)", float(window(r, sda, 2.5e-6, 3.2e-6).min()), 0.4)
    save_plot(res, r, [dict(ylabel="V", series=[("SE_SDA", sda)], limits=[("70 %", 0.7 * vdd), ("30 %", 0.3 * vdd)])],
              xscale=1e6, xlabel="time (us)")
    return res


def t_buttons():
    res = Result("buttons", "Button debounce (contact bounce on CONFIRM)",
                 "CONFIRM pressed at 1 ms with 4 bounces over 0.4 ms, released at 20 ms with 4 bounces. "
                 "GPIO input (pulls off). Logic level decided with VIL 0.8 V / VIH 2.0 V hysteresis.")
    b = [(0, 0), (1e-3, 1), (1.1e-3, 0), (1.15e-3, 1), (1.25e-3, 0), (1.3e-3, 1), (1.4e-3, 0), (1.42e-3, 1),
         (20e-3, 0), (20.08e-3, 1), (20.12e-3, 0), (20.2e-3, 1), (20.25e-3, 0), (20.33e-3, 1), (20.36e-3, 0)]
    b = [(t, v) for (t, v) in b]
    pts = []
    for (t0, v0), (t1, _) in zip(b, b[1:] + [(b[-1][0] + 1, 0)]):
        pts += [(t0, v0), (t1 - 1e-7, v0)]
    c = cfg(battery=dict(v=3.7), buttons={"SW5": pts}, gpio={g: "z" for g in ("GP12", "GP13", "GP14", "GP15")},
            analysis=".tran 2u 30m")
    r = sim(c)
    t, v = r["time"], V(r, "BTN_CONFIRM")
    state, edges, tpress, trel = 1, 0, None, None
    for ti, vi in zip(t, v):
        if state and vi < RP_VIL:
            state, edges = 0, edges + 1
            tpress = tpress or ti
        elif not state and vi > RP_VIH:
            state, edges = 1, edges + 1
            trel = ti
    res.check("FAIL", "Logic transitions seen by firmware (expect exactly 2: press + release)", edges, "", "== 2", edges == 2)
    res.le("Press detected after first contact", ((tpress or 1) - 1e-3) * 1e6, 100, "us")
    res.le("Release detected after last bounce", ((trel or 1) - 20.36e-3) * 1e3, 5, "ms")
    res.le("Pressed level", float(window(r, v, 2e-3, 19e-3).max()), RP_VIL)
    save_plot(res, r, [dict(ylabel="V", series=[("BTN_CONFIRM (GP13)", v)], limits=[("VIH 2.0", RP_VIH), ("VIL 0.8", RP_VIL)]),
                       dict(ylabel="contact", series=[("switch closed", r["v(btn_sw5)"])])], xscale=1e3, xlabel="time (ms)")
    return res


def t_vbat_adc():
    res = Result("vbat_adc", "Battery voltage measurement (GP28 / ADC2)",
                 "Battery swept 3.0-4.2 V. Firmware assumes the nominal divider ratio. The RP2350 ADC input "
                 "impedance is only specified as >= 100 k, so both an ideal ADC and a 100 k ADC are simulated.")
    ratio = 1 + N.value(N.D.P["R10"]["value"]) / N.value(N.D.P["R11"]["value"])
    worst, rows = 0.0, []
    for rin in (None, 100e3):
        for vb in (3.0, 3.3, 3.6, 3.9, 4.2):
            r = sim(cfg(battery=dict(v=vb), adc_rin=rin, gpio={"GP28": "z"}, analysis=".op"))
            true = float(V(r, "VBAT_SW")[0])
            est = float(V(r, "VBAT_SENSE")[0]) * ratio
            err = (est - true) / true * 100
            worst = max(worst, abs(err))
            rows.append((rin, vb, err))
    res.le("Worst-case uncalibrated VBAT error (ADC Rin 100 k .. ideal)", worst, 5.0, "%")
    res.info("Error with ideal ADC", max(abs(e) for rin, _, e in rows if rin is None), "%")
    res.info("Error with 100 k ADC input", max(abs(e) for rin, _, e in rows if rin), "%")
    r = sim(cfg(battery=dict(v=4.2), gpio={"GP28": "z"}, analysis=".op"))
    res.info("Divider current at 4.2 V (flows only while SW1 is ON)",
             float(V(r, "VBAT_SW")[0]) / (N.value(N.D.P["R10"]["value"]) + N.value(N.D.P["R11"]["value"])) * 1e6, "uA")
    return res


def t_usb_cc():
    res = Result("usb_cc", "USB-C CC sink advertisement (Rd = 5.1 k)",
                 "Source Rp per USB Type-C spec (pull-up to 5 V): 56 k default USB, 22 k 1.5 A, 10 k 3 A; "
                 "both plug orientations. CC voltage must fall inside the spec's vRd windows so the "
                 "charger enables VBUS and the sink can read the current capability.")
    windows = {56e3: (0.25, 0.61), 22e3: (0.70, 1.16), 10e3: (1.31, 2.04)}
    for rp, (lo, hi) in windows.items():
        for flip in (False, True):
            r = sim(cfg(battery=None, sw1=[(0, 0)], usb=dict(v=5.0, rp=rp, flip=flip), analysis=".op"))
            v = float(V(r, "USB_CC2" if flip else "USB_CC1")[0])
            res.between(f"Rp {rp / 1e3:.0f}k, {'flipped' if flip else 'normal'} plug: CC voltage", v, lo, hi)
    return res


def t_backlight():
    res = Result("backlight_pwm", "Display backlight PWM (Q4, active-low)",
                 "GP19 drives 1 kHz, 50 % duty (low = on).")
    c = cfg(battery=dict(v=3.7), gpio={"GP19": ("pwl", pwm(1e3, 0.5, 6e-3, t0=1e-3))}, analysis=".tran 0.5u 6m")
    r = sim(c)
    t, ibl = r["time"], r["i(vj4blm)"]
    on = float(window(r, ibl, 1.3e-3, 1.45e-3).mean())
    res.between("Backlight current when on", on * 1e3, 10, 40, "mA")
    m = (t > 2e-3) & (t < 5e-3)
    duty = float(np.trapezoid((ibl[m] > on / 2).astype(float), t[m]) / (t[m][-1] - t[m][0]))
    res.le("PWM duty error at 1 kHz", abs(duty - 0.5) * 100, 2.0, "% pts")
    trise = cross(t, ibl, 0.9 * on, after=2e-3)
    tstart = cross(t, ibl, 0.1 * on, after=2e-3)
    res.le("Backlight turn-on time 10-90 %", ((trise - tstart) if trise and tstart else 1) * 1e6, 20, "us")
    invariants(res, r, c)
    save_plot(res, r, [dict(ylabel="mA", series=[("backlight", ibl * 1e3)]),
                       dict(ylabel="V", series=[("LCD_BL_N (GP19)", V(r, "LCD_BL_N"))])], xscale=1e3, xlabel="time (ms)")
    return res


def t_worst_load():
    res = Result("worst_load", "Worst-case load on a low battery",
                 "Battery 3.4 V (almost empty), no USB. MCU 80 mA + BLE TX bursts 60 mA for 1 ms every 4 ms, "
                 "SD card writing 100 mA, backlight on, SE on. Pico datasheet recommends <= 300 mA from 3V3.")
    burst = [(0, 0)]
    t = 2e-3
    while t < 20e-3:
        burst += [(t, 0), (t + 10e-6, 0.06), (t + 1e-3, 0.06), (t + 1.01e-3, 0)]
        t += 4e-3
    c = cfg(battery=dict(v=3.4), mcu_load=0.08, mcu_burst=burst, card=dict(inserted=True, i=[(0, 0.1)]),
            gpio={"GP7": "low", "GP11": "low", "GP19": "low"}, analysis=".tran 5u 20m")
    r = sim(c)
    v3, vsys, i3 = V(r, "+3V3"), V(r, "VSYS"), r["i(va1im)"]
    res.ge("3V3 minimum", float(window(r, v3, 1e-3, 20e-3).min()), 3.15)
    res.ge("VSYS minimum", float(window(r, vsys, 1e-3, 20e-3).min()), 2.5)
    res.le("Peak 3V3 output current", float(i3.max()) * 1e3, 300, "mA", kind="WARN")
    res.info("Peak battery current", float(r["i(vbat)"].max()), "A")
    invariants(res, r, c)
    save_plot(res, r, [dict(ylabel="V", series=[("3V3", v3), ("VSYS", vsys)]),
                       dict(ylabel="A", series=[("3V3 load", i3), ("battery", r["i(vbat)"])])], xscale=1e3, xlabel="time (ms)")
    return res


ALL = [t_reset_default_off, t_battery_boot, t_usb_hotplug, t_switch_off_usb, t_off_drain, t_charge_cycle,
       t_sd_power, t_lcd_power, t_se_power, t_i2c, t_buttons, t_vbat_adc, t_usb_cc, t_backlight, t_worst_load]
