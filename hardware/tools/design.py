"""
Single source of truth for the Phase 1 wallet board (rev A).

Both gen_sch.py (schematic) and gen_pcb.py (PCB) are generated from this file,
so the schematic, PCB netlist and BOM can never drift apart. Edit here, then
re-run tools/build.ps1.

Conventions
- Every pin is (number, name). Numbers MUST match footprint pad numbers;
  gen_pcb.py aborts if one does not.
- NETS maps net name -> list of "REF.PIN". Pins not listed in any net must be
  listed in NC (they get a no-connect flag in the schematic).
- "lcsc_ok": True means the LCSC number is a JLCPCB basic/extended part the
  author is confident about; False means LOOK IT UP before ordering.
"""

# ---------------------------------------------------------------- part types
# Each type: symbol pin layout (left/right lists of (num, name)), footprint,
# and a body width in schematic grid units (2.54 mm).

R0603 = "Resistor_SMD:R_0603_1608Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"

TWO_PIN = dict(left=[("1", "1")], right=[("2", "2")], w=4, show_pin_names=False)

TYPES = {
    "R": dict(TWO_PIN, prefix="R"),
    "C": dict(TWO_PIN, prefix="C"),
    "LED": dict(left=[("2", "A")], right=[("1", "K")], w=4, prefix="D", show_pin_names=True),
    "FUSE": dict(TWO_PIN, prefix="F"),
    "BUTTON": dict(left=[("1", "1")], right=[("2", "2")], w=4, prefix="SW", show_pin_names=False),
    "TESTPOINT": dict(left=[("1", "TP")], right=[], w=3, prefix="TP", show_pin_names=False),
    "HOLE": dict(left=[], right=[], w=3, prefix="H", show_pin_names=False),
    "PMOS": dict(left=[("1", "G")], right=[("2", "S"), ("3", "D")], w=4, prefix="Q", show_pin_names=True),

    # Raspberry Pi Pico 2 W, castellated + underside pads (USB, SWD).
    "PICO2W": dict(
        prefix="A", w=12, show_pin_names=True,
        left=[("1", "GP0"), ("2", "GP1"), ("3", "GND"), ("4", "GP2"), ("5", "GP3"),
              ("6", "GP4"), ("7", "GP5"), ("8", "GND"), ("9", "GP6"), ("10", "GP7"),
              ("11", "GP8"), ("12", "GP9"), ("13", "GND"), ("14", "GP10"), ("15", "GP11"),
              ("16", "GP12"), ("17", "GP13"), ("18", "GND"), ("19", "GP14"), ("20", "GP15"),
              ("D1", "SWCLK"), ("D2", "GND"), ("D3", "SWDIO")],
        right=[("40", "VBUS"), ("39", "VSYS"), ("38", "GND"), ("37", "3V3_EN"), ("36", "3V3_OUT"),
               ("35", "ADC_VREF"), ("34", "GP28_A2"), ("33", "AGND"), ("32", "GP27_A1"), ("31", "GP26_A0"),
               ("30", "RUN"), ("29", "GP22"), ("28", "GND"), ("27", "GP21"), ("26", "GP20"),
               ("25", "GP19"), ("24", "GP18"), ("23", "GND"), ("22", "GP17"), ("21", "GP16"),
               ("TP1", "USB_GND"), ("TP2", "USB_DM"), ("TP3", "USB_DP"),
               ("TP4", "TP4_WL"), ("TP5", "TP5_WL"), ("TP6", "~{BOOTSEL}")]),

    # Infineon OPTIGA Trust M (datasheet rev 3.70, Table 6).
    "OPTIGA": dict(
        prefix="U", w=8, show_pin_names=True,
        left=[("10", "VCC"), ("9", "~{RST}"), ("3", "SDA"), ("8", "SCL"), ("1", "GND")],
        right=[("2", "NC"), ("4", "NC"), ("5", "NC"), ("6", "NC"), ("7", "NC")]),

    "MCP73831": dict(
        prefix="U", w=8, show_pin_names=True,
        left=[("4", "VDD"), ("5", "PROG"), ("2", "VSS")],
        right=[("3", "VBAT"), ("1", "STAT")]),

    "USBLC6": dict(
        prefix="U", w=8, show_pin_names=True,
        left=[("1", "IO1"), ("3", "IO2"), ("2", "GND")],
        right=[("6", "IO1"), ("4", "IO2"), ("5", "VBUS")]),

    "USBC": dict(
        prefix="J", w=8, show_pin_names=True,
        left=[("A4", "VBUS"), ("A9", "VBUS"), ("B4", "VBUS"), ("B9", "VBUS"),
              ("A5", "CC1"), ("B5", "CC2"), ("A8", "SBU1"), ("B8", "SBU2")],
        right=[("A6", "D+"), ("B6", "D+"), ("A7", "D-"), ("B7", "D-"),
               ("A1", "GND"), ("A12", "GND"), ("B1", "GND"), ("B12", "GND"), ("SH", "SHIELD")]),

    "MICROSD": dict(
        prefix="J", w=8, show_pin_names=True,
        left=[("4", "VDD"), ("2", "DAT3/CS"), ("3", "CMD/DI"), ("5", "CLK"), ("7", "DAT0/DO"),
              ("8", "DAT1"), ("1", "DAT2")],
        right=[("6", "VSS"), ("9", "DET_B"), ("10", "DET_A"), ("SH", "SHIELD")]),

    "JST2": dict(prefix="J", w=6, show_pin_names=True,
                 left=[("1", "BAT+"), ("2", "BAT-")], right=[]),

    "DPDT": dict(prefix="SW", w=6, show_pin_names=True,
                 left=[("2", "A_COM"), ("5", "B_COM")],
                 right=[("1", "A_OFF"), ("3", "A_ON"), ("4", "B_OFF"), ("6", "B_ON")]),

    "LCD8": dict(prefix="J", w=6, show_pin_names=True,
                 left=[("1", "GND"), ("2", "VCC"), ("3", "SCL"), ("4", "SDA"),
                       ("5", "RES"), ("6", "DC"), ("7", "CS"), ("8", "BLK")], right=[]),

    "TC2030": dict(prefix="J", w=8, show_pin_names=True,
                   left=[("1", "VCC"), ("2", "SWDIO"), ("3", "~{RESET}")],
                   right=[("4", "SWCLK"), ("5", "GND"), ("6", "SWO")]),
}

# ---------------------------------------------------------------- parts
# ref: (type, value, footprint, mpn, lcsc, lcsc_ok, group, extra)
P = {}


def part(ref, typ, value, fp, mpn="", lcsc="", lcsc_ok=True, group="", dnp=False, note=""):
    P[ref] = dict(type=typ, value=value, footprint=fp, mpn=mpn, lcsc=lcsc,
                  lcsc_ok=lcsc_ok, group=group, dnp=dnp, note=note)


# --- MCU module
part("A1", "PICO2W", "Raspberry Pi Pico 2 W", "wallet:RaspberryPi_Pico_2_W_SMD_SolderHoles",
     "SC1634 (Pico 2 W)", "C42394205", True, "mcu",
     note="JLCPCB SMT-assembles the module (extended part); check RP2350 stepping (A4 or later) on arrival")

# --- Secure element
part("U2", "OPTIGA", "OPTIGA Trust M V3", "wallet:Infineon_PG-USON-10-2_3x3mm_P0.5mm",
     "SLS32AIA010MKUSON10XTMA2", "", True, "se",
     note="Not stocked at LCSC: JLCPCB global sourcing by MPN (from an authorised distributor), or consign your own")
part("Q2", "PMOS", "AO3401A", SOT23, "AO3401A", "C15127", True, "se", note="SE power switch, active-low")
part("R12", "R", "2.2k", R0603, "0603WAF2201T5E", "C4190", True, "se",
     note="SE switch gate pull-up: must beat RP2350 reset pull-down (36k min) -> keeps Q2 OFF at boot (sim: reset_default_off)")
part("R13", "R", "4.7k", R0603, "0603WAF4701T5E", "C23162", True, "se", note="I2C SDA pull-up")
part("R14", "R", "4.7k", R0603, "0603WAF4701T5E", "C23162", True, "se", note="I2C SCL pull-up")
part("C4", "C", "100nF", C0603, "CC0603KRX7R9BB104", "C14663", True, "se")
part("C5", "C", "1uF", C0603, "CL10A105KB8NNNC", "C15849", True, "se")

# --- USB-C input
part("J1", "USBC", "USB-C 16P", "Connector_USB:USB_C_Receptacle_HRO_TYPE-C-31-M-12",
     "TYPE-C-31-M-12", "C165948", True, "usb")
part("R1", "R", "5.1k", R0603, "0603WAF5101T5E", "C23186", True, "usb", note="CC1 sink pull-down")
part("R2", "R", "5.1k", R0603, "0603WAF5101T5E", "C23186", True, "usb", note="CC2 sink pull-down")
part("F1", "FUSE", "PTC 500mA", "Fuse:Fuse_1206_3216Metric", "SMD1206-050 (JKSEMI, 6 V, 0.5 A hold, 1 A trip)", "C2846128", True, "usb")
part("U1", "USBLC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6", "USBLC6-2SC6", "C7519", True, "usb")
part("R3", "R", "0R", R0603, "0603WAF0000T5E", "C21189", True, "usb",
     note="USB D+ link. DNP = charge-only port (production option)")
part("R4", "R", "0R", R0603, "0603WAF0000T5E", "C21189", True, "usb",
     note="USB D- link. DNP = charge-only port (production option)")

# --- Charger / power path
part("U3", "MCP73831", "MCP73831T-2ACI/OT", "Package_TO_SOT_SMD:SOT-23-5",
     "MCP73831T-2ACI/OT", "C424093", True, "power")
part("C1", "C", "4.7uF", C0805, "CL21A475KAQNNNE", "C1779", True, "power", note="Charger VDD")
part("C2", "C", "4.7uF", C0805, "CL21A475KAQNNNE", "C1779", True, "power", note="Charger VBAT")
part("R5", "R", "2k", R0603, "0603WAF2001T5E", "C22975", True, "power", note="PROG: 1000/2k = 500 mA charge")
part("R6", "R", "1k", R0603, "0603WAF1001T5E", "C21190", True, "power", note="Charge LED")
part("D1", "LED", "Red", "LED_SMD:LED_0603_1608Metric", "KT-0603R", "C2286", True, "power", note="On = charging")
part("R7", "R", "10k", R0603, "0603WAF1002T5E", "C25804", True, "power", note="STAT divider top (5V -> 3.3V)")
part("R8", "R", "15k", R0603, "0603WAF1502T5E", "C22809", True, "power",
     note="STAT divider bottom: 5.5 V VBUS -> 3.30 V, below 3.63 V unpowered-FT limit (sim: switch_off_usb)")
part("J2", "JST2", "LiPo 1S", "Connector_JST:JST_PH_S2B-PH-SM4-TB_1x02-1MP_P2.00mm_Horizontal",
     "S2B-PH-SM4-TB(LF)(SN)", "C295747", True, "power",
     note="CHECK battery lead polarity: pin1 = BAT+. Use a cell WITH protection PCM")
part("SW1", "DPDT", "Power", "Button_Switch_SMD:SW_DPDT_CK_JS202011JCQN", "JS202011JCQN", "C221665", True, "power",
     note="Pole A disconnects battery, pole B grounds 3V3_EN (kills 3V3 even on USB)")
part("Q1", "PMOS", "AO3401A", SOT23, "AO3401A", "C15127", True, "power",
     note="Load-sharing: battery->VSYS when USB absent (Pico datasheet 4.6)")
part("R9", "R", "100k", R0603, "0603WAF1003T5E", "C25803", True, "power", note="Q1 gate pull-down")
part("C3", "C", "10uF", C0805, "CL21A106KAYNNNE", "C15850", True, "power", note="VSYS bulk")
part("R10", "R", "4.7k", R0603, "0603WAF4701T5E", "C23162", True, "power",
     note="VBAT sense divider: low impedance vs ADC Rin >= 100k (sim: vbat_adc). 0.45 mA, only while SW1 ON")
part("R11", "R", "4.7k", R0603, "0603WAF4701T5E", "C23162", True, "power", note="VBAT sense divider")
part("C16", "C", "100nF", C0603, "CC0603KRX7R9BB104", "C14663", True, "power", note="ADC filter")
part("C14", "C", "10uF", C0805, "CL21A106KAYNNNE", "C15850", True, "power", note="3V3 bulk")
part("C15", "C", "100nF", C0603, "CC0603KRX7R9BB104", "C14663", True, "power")

# --- microSD
part("J3", "MICROSD", "microSD", "Connector_Card:microSD_HC_Hirose_DM3AT-SF-PEJM5",
     "DM3AT-SF-PEJM5", "C114218", True, "sd", note="Not the DM3AT-SF-PEJM5(40) variant (C597971)")
part("Q3", "PMOS", "AO3401A", SOT23, "AO3401A", "C15127", True, "sd", note="SD power switch (card unpowered unless backing up)")
part("R21", "R", "2.2k", R0603, "0603WAF2201T5E", "C4190", True, "sd",
     note="Q3 gate pull-up: beats RP2350 reset pull-down -> SD OFF at boot (sim: reset_default_off)")
part("R23", "R", "470", R0603, "0603WAF4700T5E", "C23179", True, "sd", note="Q3 gate series (soft start with C8)")
part("C8", "C", "4.7uF", C0603, "CL10A475KO8NNNC", "C19666", True, "sd",
     note="Q3 soft-start Cgs: SD_VDD ramp must be >= 0.1 ms (SD spec) (sim: sd_power)")
part("C6", "C", "10uF", C0805, "CL21A106KAYNNNE", "C15850", True, "sd")
part("C7", "C", "100nF", C0603, "CC0603KRX7R9BB104", "C14663", True, "sd")
for r, n in [("R15", "CS"), ("R16", "CMD"), ("R17", "DAT0"), ("R18", "DAT1"), ("R19", "DAT2")]:
    part(r, "R", "10k", R0603, "0603WAF1002T5E", "C25804", True, "sd", note=f"SD {n} pull-up to SD_VDD")
part("R20", "R", "10k", R0603, "0603WAF1002T5E", "C25804", True, "sd", note="Card-detect pull-up")

# --- Display
part("J4", "LCD8", "LCD 2.0in ST7789", "Connector_PinSocket_2.54mm:PinSocket_1x08_P2.54mm_Vertical",
     "2.54-1*8P female header, 8.5 mm (BOOMELE)", "C27438", True, "lcd",
     note="Pin order must match YOUR module (common: GND VCC SCL SDA RES DC CS BLK)")
part("Q4", "PMOS", "AO3401A", SOT23, "AO3401A", "C15127", True, "lcd", note="Backlight high-side switch")
part("R25", "R", "2.2k", R0603, "0603WAF2201T5E", "C4190", True, "lcd",
     note="Backlight gate pull-up: beats RP2350 reset pull-down -> backlight OFF at boot")
part("C9", "C", "10uF", C0805, "CL21A106KAYNNNE", "C15850", True, "lcd")
# Display module power switch: in sleep the whole module (controller + its regulator/LED driver)
# is unpowered. Without it the module's logic draws ~8 mA from +3V3 forever (sim: off_drain).
part("Q5", "PMOS", "AO3401A", SOT23, "AO3401A", "C15127", True, "lcd",
     note="LCD module power switch (sleep mode), active-low on GP18")
part("R30", "R", "2.2k", R0603, "0603WAF2201T5E", "C4190", True, "lcd",
     note="Q5 gate pull-up: beats RP2350 reset pull-down -> display unpowered at boot (sim: reset_default_off)")
part("R31", "R", "470", R0603, "0603WAF4700T5E", "C23179", True, "lcd", note="Q5 gate series (soft start with C17)")
part("C17", "C", "4.7uF", C0603, "CL10A475KO8NNNC", "C19666", True, "lcd",
     note="Q5 soft-start Cgs: limits inrush into C9 + module so +3V3 does not dip (sim: lcd_power)")

# --- Buttons
for i, (sw, r, c, name) in enumerate([("SW2", "R26", "C10", "UP"), ("SW3", "R27", "C11", "DOWN"),
                                      ("SW4", "R28", "C12", "BACK"), ("SW5", "R29", "C13", "CONFIRM")]):
    part(sw, "BUTTON", name, "Button_Switch_SMD:SW_Push_1P1T_XKB_TS-1187A", "TS-1187A-B-A-B", "C318884", True, "buttons")
    part(r, "R", "10k", R0603, "0603WAF1002T5E", "C25804", True, "buttons", note=f"{name} pull-up")
    part(c, "C", "100nF", C0603, "CC0603KRX7R9BB104", "C14663", True, "buttons", note=f"{name} debounce")

# --- Debug (prototype only; leave unpopulated/covered in production)
part("J5", "TC2030", "SWD (Tag-Connect)", "Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical",
     "No part: pogo cable TC2030-IDC-NL", "", True, "debug", dnp=True)
TP = "TestPoint:TestPoint_Pad_D1.0mm"
for tp, n in [("TP1", "GND"), ("TP2", "+3V3"), ("TP3", "VSYS"), ("TP4", "VBAT"),
              ("TP5", "UART_TX"), ("TP6", "UART_RX"), ("TP7", "GP4"), ("TP8", "LCD_PWR_N")]:
    part(tp, "TESTPOINT", n, TP, "No part (bare pad)", "", True, "debug", dnp=True)

for h in ["H1", "H2", "H3", "H4"]:
    part(h, "HOLE", "M2", "MountingHole:MountingHole_2.2mm_M2", "No part", "", True, "mech", dnp=True)

# ---------------------------------------------------------------- nets
NETS = {
    "GND": ["A1.3", "A1.8", "A1.13", "A1.18", "A1.23", "A1.28", "A1.33", "A1.38", "A1.D2", "A1.TP1",
            "U2.1", "C4.2", "C5.2",
            "J1.A1", "J1.A12", "J1.B1", "J1.B12", "J1.SH", "R1.2", "R2.2", "U1.2",
            "U3.2", "C1.2", "C2.2", "R5.2", "R8.2", "J2.2", "SW1.4", "R9.2", "C3.2",
            "R11.2", "C16.2", "C14.2", "C15.2",
            "J3.6", "J3.10", "J3.SH", "C6.2", "C7.2",
            "J4.1", "C9.2",
            "SW2.2", "SW3.2", "SW4.2", "SW5.2", "C10.2", "C11.2", "C12.2", "C13.2",
            "J5.5", "TP1.1"],
    "+3V3": ["A1.36", "Q2.2", "R12.1", "R13.1", "R14.1", "C14.1", "C15.1",
             "Q3.2", "R21.1", "C8.1", "R20.1", "Q4.2", "R25.1",
             "R26.1", "R27.1", "R28.1", "R29.1", "J5.1", "TP2.1"],
    "VBUS": ["A1.40", "F1.2", "U1.5", "U3.4", "C1.1", "R6.1", "Q1.1", "R9.1"],
    "VBUS_C": ["J1.A4", "J1.A9", "J1.B4", "J1.B9", "F1.1"],
    "VSYS": ["A1.39", "Q1.2", "C3.1", "TP3.1"],
    "VBAT": ["U3.3", "C2.1", "J2.1", "SW1.2", "TP4.1"],
    "VBAT_SW": ["SW1.3", "Q1.3", "R10.1"],
    "3V3_EN": ["A1.37", "SW1.5"],
    "RUN": ["A1.30", "J5.3"],
    "SWDIO": ["A1.D3", "J5.2"],
    "SWCLK": ["A1.D1", "J5.4"],
    "USB_CC1": ["J1.A5", "R1.1"],
    "USB_CC2": ["J1.B5", "R2.1"],
    "USB_DP_C": ["J1.A6", "J1.B6", "U1.1", "U1.6", "R3.1"],
    "USB_DM_C": ["J1.A7", "J1.B7", "U1.3", "U1.4", "R4.1"],
    "USB_DP": ["R3.2", "A1.TP3"],
    "USB_DM": ["R4.2", "A1.TP2"],
    "CHG_LED": ["R6.2", "D1.2"],
    "CHG_STAT_5V": ["U3.1", "D1.1", "R7.1"],
    "CHG_STAT": ["R7.2", "R8.1", "A1.7"],
    "CHG_PROG": ["U3.5", "R5.1"],
    "VBAT_SENSE": ["R10.2", "R11.1", "C16.1", "A1.34"],
    # Secure element
    "SE_VCC": ["Q2.3", "U2.10", "C4.1", "C5.1"],
    "SE_PWR_N": ["A1.15", "Q2.1", "R12.2"],
    "SE_RST_N": ["A1.14", "U2.9"],
    "SE_SDA": ["A1.11", "U2.3", "R13.2"],
    "SE_SCL": ["A1.12", "U2.8", "R14.2"],
    # microSD (SPI1)
    "SD_VDD": ["Q3.3", "J3.4", "C6.1", "C7.1", "R15.1", "R16.1", "R17.1", "R18.1", "R19.1"],
    "SD_PWR_N": ["A1.10", "R23.1"],
    "SD_GATE": ["R23.2", "Q3.1", "R21.2", "C8.2"],
    "SD_SCK": ["A1.4", "J3.5"],
    "SD_MOSI": ["A1.5", "J3.3", "R16.2"],
    "SD_MISO": ["A1.1", "J3.7", "R17.2"],
    "SD_CS": ["A1.2", "J3.2", "R15.2"],
    "SD_DAT1": ["J3.8", "R18.2"],
    "SD_DAT2": ["J3.1", "R19.2"],
    "SD_DET": ["A1.9", "J3.9", "R20.2"],
    # Display (SPI0)
    "LCD_SCK": ["A1.31", "J4.3"],
    "LCD_MOSI": ["A1.32", "J4.4"],
    "LCD_RST": ["A1.26", "J4.5"],
    "LCD_DC": ["A1.29", "J4.6"],
    "LCD_CS": ["A1.27", "J4.7"],
    "LCD_BL_N": ["A1.25", "Q4.1", "R25.2"],
    "LCD_BLK": ["Q4.3", "J4.8"],
    # Buttons
    "BTN_UP": ["A1.19", "SW2.1", "R26.2", "C10.1"],
    "BTN_DOWN": ["A1.20", "SW3.1", "R27.2", "C11.1"],
    "BTN_BACK": ["A1.16", "SW4.1", "R28.2", "C12.1"],
    "BTN_CONFIRM": ["A1.17", "SW5.1", "R29.2", "C13.1"],
    # Debug
    "UART_TX": ["A1.21", "TP5.1"],
    "UART_RX": ["A1.22", "TP6.1"],
    "GP4": ["A1.6", "TP7.1"],
    "LCD_PWR_N": ["A1.24", "R31.1", "TP8.1"],  # GP18: next to GP19 (backlight) with the other display pins
    "LCD_GATE": ["R31.2", "Q5.1", "R30.2", "C17.2"],
    "LCD_VCC": ["Q5.3", "J4.2", "C9.1"],
}
# Display VCC is +3V3 (C9 is local bulk at the header).
NETS["+3V3"] += ["Q5.2", "R30.1", "C17.1"]

NC = ["A1.35", "A1.TP4", "A1.TP5", "A1.TP6",
      "U2.2", "U2.4", "U2.5", "U2.6", "U2.7",
      "J1.A8", "J1.B8", "SW1.1", "SW1.6", "J5.6"]

GROUP_TITLES = {
    "mcu": "MCU: Raspberry Pi Pico 2 W (RP2350 + CYW43439)",
    "se": "Secure element: Infineon OPTIGA Trust M V3 (I2C0 GP8/9, switchable VCC)",
    "usb": "USB-C: power + data (R3/R4 DNP = charge-only)",
    "power": "Battery, charger, load-sharing, hard power switch",
    "sd": "microSD (SPI0 GP0-3), power-gated",
    "lcd": "Display: 2.0in ST7789 IPS module (SPI1 GP26/27)",
    "buttons": "Buttons (active low, external pull-ups: immune to RP2350-E9)",
    "debug": "Debug: SWD Tag-Connect + test points (prototype only)",
    "mech": "Mechanical",
}


def all_pins(ref):
    t = TYPES[P[ref]["type"]]
    return t["left"] + t["right"]


def check():
    seen = {}
    for net, pins in NETS.items():
        assert len(pins) >= 2, f"net {net} has <2 pins"
        for rp in pins:
            ref, pin = rp.split(".", 1)
            assert ref in P, f"{rp}: unknown ref"
            nums = [n for n, _ in all_pins(ref)]
            assert pin in nums, f"{rp}: unknown pin"
            assert rp not in seen, f"{rp} in both {seen[rp]} and {net}"
            seen[rp] = net
    for rp in NC:
        assert rp not in seen, f"{rp} marked NC but in net {seen[rp]}"
    for ref in P:
        for n, _ in all_pins(ref):
            rp = f"{ref}.{n}"
            assert rp in seen or rp in NC, f"{rp} unconnected and not marked NC"
    return seen


if __name__ == "__main__":
    s = check()
    print(f"OK: {len(P)} parts, {len(NETS)} nets, {len(s)} connected pins, {len(NC)} NC")
