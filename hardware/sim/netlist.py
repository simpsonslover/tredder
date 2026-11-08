"""
Build a SPICE netlist of the whole board straight from tools/design.py.

Every resistor, capacitor, MOSFET, LED, switch and connection comes from the
design file, so the simulation tests the real schematic, not a hand copy.
ICs and modules are behavioural models (see MODEL NOTES below); test cases
add the outside world (USB source, battery state, GPIO firmware behaviour,
button presses) through a Config dict.

MODEL NOTES (sources / assumptions)
- RP2350 pads: reset state = input disabled + pull-DOWN 36..113 k (RP2350
  datasheet 9.x "pad reset state", Table 1683 RPD). FT pins tolerate 5.5 V
  when IOVDD = 3.3 V and 3.63 V when unpowered (Table 1680). ADC pins
  (GP26-29) are not FT: clamp diodes to IOVDD. ADC input impedance >= 100 k
  (Table 1685). VIH 2.0 V, VIL 0.8 V.
- Pico 2 W: VBUS -> Schottky -> VSYS; 3V3_EN pulled up to VSYS by 100 k;
  RT6154 buck-boost modelled as an ideal 3.3 V source (88 % efficient,
  soft-start ~0.5 ms, high-impedance when disabled). VSYS/3 divider 300 k.
  Module capacitances (10 uF VSYS, 22 uF 3V3) are ASSUMPTIONS.
- MCP73831-2: Ireg = 1000 V / Rprog, CV 4.20 V, 10 % precondition below
  3.0 V, STAT push-pull low = charging / high = complete (< 7.5 % Ireg),
  Hi-Z when not powered. UVLO 3.75 V. (Microchip DS20001984.) Termination
  does not stop charge in the model (STAT only).
- LiPo: OCV table, 120 mOhm internal resistance, capacity scalable.
- AO3401A: level-1 PMOS, Vth corner selectable (-0.5 / -0.9 / -1.3 V per
  AOS datasheet), Rds(on) ~50 mOhm @ -4.5 V, Ciss ~700 pF, body diode.
- NXP SE050: up to 19 mA (AES + public-key coprocessors together, datasheet
  Table 12) from VIN/VCC; SDA/SCL open-drain, 10.5 pF, weak internal pull-up
  70..265 uA to VIN (Table 11, modelled as 18 k); clamp to VSS only (leakage
  <= 15 uA at 3.6 V, no clamp to VIN is specified -> ASSUMED none).
- microSD card: 4.7 uF internal VDD capacitance (ASSUMPTION), configurable
  load current, I/O clamp diodes to card VDD.
- Display module: 8 mA logic load, 10 uF on module (ASSUMPTION), I/O clamps
  to VCC, backlight = white LED (Vf ~2.9 V) + 10 Ohm on the module.
- USB source: ideal 5 V (configurable) behind cable R/L, hot-plug switch,
  Rp on CC per USB Type-C spec (56 k default, 22 k 1.5 A, 10 k 3 A to 5 V).
- USBLC6-2SC6: I/O steering diodes + VBUS clamp ~6.5 V (rough).
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
import design as D  # noqa: E402

PIN_NET = D.check()
PARTS = D.P        # board being simulated; sim/breadboard.py swaps in the breadboard's parts + nets
EMITTERS = {}      # extra part types: type -> fn(cfg, ref, n, L)

# ---------------------------------------------------------------- helpers

UNITS = {"p": 1e-12, "n": 1e-9, "u": 1e-6, "m": 1e-3, "k": 1e3, "M": 1e6, "R": 1.0}


def value(s):
    """'4.7k' -> 4700, '100nF' -> 1e-7, '0R' -> 0, '10uF' -> 1e-5, '2k' -> 2000."""
    m = re.match(r"^\s*([\d.]+)\s*([pnumkMR]?)", s)
    if not m:
        raise ValueError(s)
    return float(m.group(1)) * UNITS.get(m.group(2), 1.0)


def node(net):
    if net == "GND":
        return "0"
    return "n_" + re.sub(r"[^a-z0-9]", "_", net.lower().replace("+", "p"))


def pin_node(ref, pin):
    rp = f"{ref}.{pin}"
    return node(PIN_NET[rp]) if rp in PIN_NET else f"nc_{ref}_{pin}".lower()


def pins_by_name(ref):
    t = D.TYPES[D.P[ref]["type"]]
    return {name: num for num, name in t["left"] + t["right"]}


def s(x, x0, w):
    """Smooth 0..1 step for behavioural expressions."""
    return f"(0.5*(1+tanh(({x}-({x0}))/({w}))))"


def pwl(points):
    return "PWL(" + " ".join(f"{t:g} {v:g}" for t, v in points) + ")"


PICO = "A1"
PICO_PINS = pins_by_name(PICO)
GPIO_PIN = {}  # 'GP7' -> pad number
for nm, num in PICO_PINS.items():
    m = re.match(r"GP(\d+)", nm)
    if m:
        GPIO_PIN[f"GP{m.group(1)}"] = num
ADC_GPIOS = {"GP26", "GP27", "GP28"}


def gpio_node(gp):
    return pin_node(PICO, GPIO_PIN[gp])


def net_of_gpio(gp):
    return PIN_NET.get(f"{PICO}.{GPIO_PIN[gp]}")


VTH = {"min": -0.5, "typ": -0.9, "max": -1.3}

MODELS = """
.model DSCH D(IS=2e-6 N=1.05 RS=0.08 CJO=100p)
.model DRED D(IS=1e-18 N=2 RS=5)
.model DWHITE D(IS=8.7e-27 N=2 RS=2)
.model DESD D(IS=1e-14 N=1 RS=1)
.model DZFT D(IS=1e-14 N=1 BV=5.8 IBV=1e-6 RS=2)
.model DZUSB D(IS=1e-14 N=1 BV=6.5 IBV=1e-3 RS=1.5)
.model DBODY D(IS=1e-12 N=1 RS=0.02 CJO=200p)
.model SWM SW(VT=0.5 VH=0.05 RON=0.05 ROFF=1e9)
.model SWBTN SW(VT=0.5 VH=0.05 RON=0.1 ROFF=1e9)
.model SWOD SW(VT=0.5 VH=0.05 RON=40 ROFF=1e9)
.model SWREG SW(VT=0.05 VH=0.01 RON=0.01 ROFF=1e9)
"""

LIPO_OCV = [(0.0, 3.00), (0.05, 3.45), (0.10, 3.60), (0.20, 3.68), (0.40, 3.75),
            (0.60, 3.85), (0.80, 4.00), (1.00, 4.20)]


def ocv_expr(x):
    expr = f"{LIPO_OCV[-1][1]}"
    for (x0, y0), (x1, y1) in reversed(list(zip(LIPO_OCV, LIPO_OCV[1:]))):
        lin = f"({y0}+({y1}-{y0})*(({x})-{x0})/({x1}-{x0}))"
        expr = f"(({x})<{x1} ? {lin} : {expr})"
    return f"(({x})<0 ? {LIPO_OCV[0][1]} : {expr})"


def soc_for_ocv(v):
    for (x0, y0), (x1, y1) in zip(LIPO_OCV, LIPO_OCV[1:]):
        if y0 <= v <= y1:
            return x0 + (x1 - x0) * (v - y0) / (y1 - y0)
    return 0.0 if v < LIPO_OCV[0][1] else 1.0


# ---------------------------------------------------------------- default config

def default_config():
    return dict(
        title="wallet",
        usb=None,                  # dict(v=5.0, plug=[(t, 0/1)...], R=0.1, L=0.5e-6, rp=56e3, flip=False)
        battery=dict(v=3.7, cap_mah=1000, rint=0.12),   # or None, or dict(soc=..)
        sw1=[(0, 1)],              # power switch: 1 = ON
        gpio={},                   # 'GP7': 'low'|'high'|'z'|'pu'|('pwl',pts)|('od',pts) ; default 'reset'
        rpd=36e3,                  # RP2350 reset pull-down (36k worst-case strong .. 113k)
        rpu=32e3,
        vth="typ",
        mcu_load=0.06,             # A from 3V3 (RP2350 + flash + CYW43 idle-ish)
        mcu_burst=None,            # PWL of extra A (radio TX bursts)
        card=dict(inserted=True, i=[(0, 0.0005)]),
        adc_rin=None,              # Ohms from GP28 to GND (ADC sampling), None = ideal
        buttons={},                # 'SW5': [(t, 0/1)...]
        lcd_load=0.008,
        fuse_r=0.3,                # PTC resistance: 0.5 A-hold 1206 parts are ~0.15 .. 1.0 Ohm
        analysis=".tran 10u 10m",
        extra=[],
    )


# ---------------------------------------------------------------- emitters

def emit_parts(cfg, L):
    vth = VTH[cfg["vth"]] if isinstance(cfg["vth"], str) else cfg["vth"]
    L.append(f".model PAO3401 PMOS(LEVEL=1 VTO={vth} KP=5.6 LAMBDA=0.01)")
    for ref, p in PARTS.items():
        t = p["type"]
        n = lambda pin: pin_node(ref, pin)  # noqa: E731
        if t == "R":
            v = value(p["value"])
            L.append(f"R{ref} {n('1')} {n('2')} {max(v, 1e-3):g}")
        elif t == "C":
            L.append(f"C{ref} {n('1')} {n('2')} {value(p['value']):g}")
        elif t == "DIODE":                 # 1 = anode, 2 = cathode (Schottky)
            L.append(f"D{ref} {n('1')} {n('2')} DSCH")
        elif t == "LED":
            L.append(f"D{ref} {n('2')} {n('1')} DRED")
        elif t == "FUSE":
            L.append(f"R{ref} {n('1')} {n('2')} {cfg['fuse_r']:g}")
        elif t == "PMOS":
            g, s_, d = n("1"), n("2"), n("3")
            L.append(f"M{ref} {d} {g} {s_} {s_} PAO3401 W=1u L=1u")
            L.append(f"D{ref}b {d} {s_} DBODY")
            L.append(f"C{ref}gs {g} {s_} 600p")
            L.append(f"C{ref}gd {g} {d} 100p")
        elif t == "BUTTON":
            pts = cfg["buttons"].get(ref, [(0, 0)])
            L.append(f"V{ref}c btn_{ref.lower()} 0 {pwl(pts)}")
            L.append(f"S{ref} {n('1')} {n('2')} btn_{ref.lower()} 0 SWBTN")
        elif t == "DPDT":
            L.append(f"V{ref}c sw_{ref.lower()} 0 {pwl(cfg['sw1'])}")
            L.append(f"B{ref}i swi_{ref.lower()} 0 V=1-V(sw_{ref.lower()})")
            L.append(f"S{ref}a {n('2')} {n('3')} sw_{ref.lower()} 0 SWM")      # ON: 2-3
            L.append(f"S{ref}b {n('5')} {n('6')} sw_{ref.lower()} 0 SWM")      # ON: 5-6
            L.append(f"S{ref}c {n('2')} {n('1')} swi_{ref.lower()} 0 SWM")     # OFF: 1-2
            L.append(f"S{ref}d {n('5')} {n('4')} swi_{ref.lower()} 0 SWM")     # OFF: 4-5
        elif t in ("TESTPOINT", "HOLE", "TC2030"):
            pass
        elif t == "PICO2W":
            emit_pico(cfg, L)
        elif t == "SE050":
            vin = n("12")                      # VIN, VCC and ENA all on SE_VCC
            L += [f"B{ref}ld {vin} 0 I=0.019*{s(f'V({vin})', 1.6, 0.03)}",
                  f"C{ref}in {vin} 0 100p"]
            for pin in ("9", "10"):  # SDA, SCL: open-drain, weak pull-up to VIN, clamp to VSS only
                L += [f"C{ref}p{pin} {n(pin)} 0 10.5p", f"D{ref}g{pin} 0 {n(pin)} DESD",
                      f"R{ref}wpu{pin} {n(pin)} {vin} 18k"]
        elif t == "MCP73831":
            emit_charger(ref, n, L)
        elif t == "USBLC6":
            vb, gnd = n("5"), n("2")
            for pin in ("1", "3"):
                L += [f"D{ref}h{pin} {n(pin)} {vb} DESD", f"D{ref}l{pin} {gnd} {n(pin)} DESD"]
            L.append(f"D{ref}z {gnd} {vb} DZUSB")
        elif t == "USBC":
            emit_usb(cfg, ref, n, L)
        elif t == "MICROSD":
            emit_card(cfg, ref, n, L)
        elif t == "JST2":
            emit_battery(cfg, ref, n, L)
        elif t == "LCD8":
            vcc = n("2")
            L += [f"B{ref}ld {vcc} 0 I={cfg['lcd_load']}*{s(f'V({vcc})', 2.5, 0.05)}",
                  f"C{ref}m {vcc} 0 10u"]
            for pin in ("3", "4", "5", "6", "7"):
                L += [f"C{ref}p{pin} {n(pin)} 0 10p", f"D{ref}h{pin} {n(pin)} {vcc} DESD",
                      f"D{ref}l{pin} 0 {n(pin)} DESD"]
            L += [f"D{ref}bl {n('8')} {ref.lower()}_bl DWHITE", f"R{ref}bl {ref.lower()}_bl {ref.lower()}_bl0 10",
                  f"V{ref}blm {ref.lower()}_bl0 0 0"]
        elif t in EMITTERS:
            EMITTERS[t](cfg, ref, n, L)
        else:
            raise SystemExit(f"no simulation model for type {t} ({ref})")


def emit_pico(cfg, L):
    n = lambda name: pin_node(PICO, PICO_PINS[name])  # noqa: E731
    vbus, vsys, en, v3 = n("VBUS"), n("VSYS"), n("3V3_EN"), n("3V3_OUT")
    L += [
        "* --- Pico 2 W module (behavioural) ---",
        f"DA1vb {vbus} {vsys} DSCH",
        f"CA1vsys {vsys} 0 10u",
        f"RA1vdiv {vsys} 0 300k",
        f"RA1en {vsys} {en} 100k",
        f"BA1ctl a1_ctl 0 V={s(f'V({en})', 1.0, 0.03)}*{s(f'V({vsys})', 1.8, 0.03)}",
        "RA1ss a1_ctl a1_ctlf 1k",
        "CA1ss a1_ctlf 0 0.5u",
        "BA1reg a1_rega 0 V=3.3*min(1,V(a1_ctlf)*1.1)",
        "VA1im a1_rega a1_regb 0",
        "RA1out a1_regb a1_regc 0.03",
        f"SA1en a1_regc {v3} a1_ctlf 0 SWREG",
        f"BA1in {vsys} 0 I=V({v3})*max(i(VA1im),0)/(0.88*max(V({vsys}),1))",
        f"CA1out {v3} 0 22u",
        f"BA1mcu {v3} 0 I={cfg['mcu_load']}*{s(f'V({v3})', 2.7, 0.05)}",
    ]
    if cfg["mcu_burst"]:
        L += [f"VA1bst a1_burst 0 {pwl(cfg['mcu_burst'])}",
              f"BA1bst {v3} 0 I=V(a1_burst)*{s(f'V({v3})', 2.7, 0.05)}"]
    for gp, num in GPIO_PIN.items():
        p = pin_node(PICO, num)
        L.append(f"CA1{gp} {p} 0 5p")
        if gp in ADC_GPIOS:
            L += [f"DA1h{gp} {p} {v3} DESD", f"DA1l{gp} 0 {p} DESD"]
        else:
            L.append(f"DA1z{gp} 0 {p} DZFT")
        mode = cfg["gpio"].get(gp, "reset")
        if mode == "reset":
            L.append(f"BA1pd{gp} {p} 0 I=V({p})/{cfg['rpd']:g}*{s(f'V({v3})', 2.0, 0.1)}")
        elif mode == "pu":
            L.append(f"BA1pu{gp} {v3} {p} I=(V({v3})-V({p}))/{cfg['rpu']:g}*{s(f'V({v3})', 2.0, 0.1)}")
        elif mode == "z":
            pass
        elif isinstance(mode, tuple) and mode[0] == "od":
            L += [f"VA1od{gp} a1_od_{gp.lower()} 0 {pwl(mode[1])}",
                  f"SA1od{gp} {p} 0 a1_od_{gp.lower()} 0 SWOD"]
        else:
            if mode == "low":
                pts = [(0, 0)]
            elif mode == "high":
                pts = [(0, 3.3)]
            else:
                assert mode[0] == "pwl", mode
                pts = mode[1]
            L += [f"VA1c{gp} a1_c_{gp.lower()} 0 {pwl(pts)}",
                  f"BA1d{gp} a1_d_{gp.lower()} 0 V=max(0,min(V(a1_c_{gp.lower()}),V({v3})))",
                  f"RA1d{gp} a1_d_{gp.lower()} {p} 40"]
    if cfg["adc_rin"]:
        L.append(f"RA1adc {gpio_node('GP28')} 0 {cfg['adc_rin']:g}")


def emit_charger(ref, n, L):
    vdd, vss, vbat, stat, prog = n("4"), n("2"), n("3"), n("1"), n("5")
    r = ref.lower()
    ireg = f"(1000*(V({r}_pa)-V({prog})))"
    en = f"({s(f'V({vdd})', 3.75, 0.02)}*{s(f'V({vdd})-V({vbat})', 0.12, 0.01)})"
    cv = f"max(0,min(1,(4.2-V({vbat}))/0.01))"
    pre = f"(0.1+0.9*{s(f'V({vbat})', 2.95, 0.02)})"
    done = s(f"0.075-{cv}*{pre}", 0, 0.005)
    L += [
        f"* --- {ref} MCP73831 (behavioural) ---",
        f"V{ref}ref {r}_pa {vss} 1.0",
        f"R{ref}s {r}_pa {prog} 1",
        f"B{ref}chg {vdd} {vbat} I={ireg}*{en}*{cv}*{pre}",
        f"R{ref}q {vdd} {vss} 10k",
        f"R{ref}dr {vbat} {vss} 4Meg",
        f"B{ref}en {r}_en {vss} V={en}",
        f"B{ref}st {r}_st {vss} V=V({vdd})*{done}",
        f"R{ref}st {r}_st {r}_sto 50",
        f"S{ref}st {r}_sto {stat} {r}_en {vss} SWM",
    ]


def emit_usb(cfg, ref, n, L):
    u = cfg["usb"]
    if not u:
        return
    vb = n("A4")
    plug = u.get("plug", [(0, 1)])
    cc_on, cc_off = (n("B5"), n("A5")) if u.get("flip") else (n("A5"), n("B5"))
    L += [
        "* --- USB source + cable ---",
        f"V{ref}src usb_src 0 {pwl(u['v']) if isinstance(u.get('v'), list) else u.get('v', 5.0)}",
        f"R{ref}cab usb_src usb_l {u.get('R', 0.1):g}",
        f"L{ref}cab usb_l usb_sw {u.get('L', 0.5e-6):g}",
        f"V{ref}plug usb_plug 0 {pwl(plug)}",
        f"S{ref}plug usb_sw {vb} usb_plug 0 SWM",
        f"B{ref}cc usb_cc 0 V=5*V(usb_plug)",
        f"R{ref}rp usb_cc {cc_on} {u.get('rp', 56e3):g}",
    ]


def emit_card(cfg, ref, n, L):
    c = cfg["card"]
    vdd = n("4")
    if c.get("inserted", True):
        L += [f"C{ref}int {vdd} 0 4.7u",
              f"V{ref}i {ref.lower()}_i 0 {pwl(c.get('i', [(0, 0.0005)]))}",
              f"B{ref}ld {vdd} 0 I=V({ref.lower()}_i)*{s(f'V({vdd})', 2.5, 0.05)}"]
        for pin in ("1", "2", "3", "5", "7", "8"):
            L += [f"C{ref}p{pin} {n(pin)} 0 10p", f"D{ref}h{pin} {n(pin)} {vdd} DESD",
                  f"D{ref}l{pin} 0 {n(pin)} DESD"]
        L.append(f"R{ref}det {n('9')} {n('10')} 0.1")


def emit_battery(cfg, ref, n, L):
    b = cfg["battery"]
    if not b:
        return
    soc0 = b["soc"] if "soc" in b else soc_for_ocv(b["v"])
    q = b.get("cap_mah", 1000) * 3.6
    plus, minus = n("1"), n("2")   # minus is whatever the wiring connects it to, not assumed GND
    L += [
        "* --- LiPo cell ---",
        "Csoc bat_soc 0 " + f"{q:g}",
        f"Bocv bat_ocv {minus} V={ocv_expr('V(bat_soc)')}",
        f"Rint bat_ocv bat_mid {b.get('rint', 0.12):g}",
        f"Vbat bat_mid {plus} 0",
        "Bsoc 0 bat_soc I=-i(Vbat)",
        f".ic V(bat_soc)={soc0:g}",
    ]


def build(cfg):
    L = [f"* {cfg['title']}", ".options rshunt=1e9 reltol=1e-3 method=gear itl4=200"]
    L += MODELS.strip().splitlines()
    emit_parts(cfg, L)
    L += cfg["extra"]
    L.append(cfg["analysis"])
    L.append(".end")
    return "\n".join(L)


if __name__ == "__main__":
    print(build(default_config()))
