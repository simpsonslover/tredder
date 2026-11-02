/*
 * Behavioural model of BT1 (1S LiPo 503035, 500 mAh) + the MCP73831 charger
 * (U3, 500 mA CC then 4.20 V CV, done at C/10) as the firmware sees them:
 * VBAT on the ADC, CHG_STAT, USB present.
 *
 * Load currents are estimates for the board, not measurements:
 *   awake  ~45 mA (RP2350 + regulator) + ~8 mA display logic + ~18 mA backlight
 *   sleep  2.6 mA (hardware/sim off_drain: MCU dormant 1 mA assumed, display unpowered)
 *   off    ~0 (SW1 OFF: 0.8 uA, sim off_drain)
 * --battery-speed N runs the cell N times faster than real time so a
 * discharge / charge can be watched.
 */
#include <math.h>

#include "app/hal.h"
#include "emu.h"

#define CAP_MAH 500.0
#define R_INT 0.15 /* ohm, cell + PCM */

static struct {
  double soc; /* 0..1 */
  bool usb;
  uint32_t last_ms;
  double load_ma, chg_ma;
} B = {0.8, false, 0, 0, 0};

/* open-circuit voltage of the cell (typical LiPo curve) */
static double ocv(double soc) {
  static const double S[] = {0, 0.05, 0.12, 0.2, 0.3, 0.4, 0.5, 0.65, 0.78, 0.9, 1.0};
  static const double V[] = {3.30, 3.50, 3.60, 3.65, 3.70, 3.75, 3.80, 3.90, 4.00, 4.10, 4.20};
  if (soc <= 0) return V[0];
  for (int i = 1; i < 11; i++)
    if (soc <= S[i]) return V[i - 1] + (soc - S[i - 1]) * (V[i] - V[i - 1]) / (S[i] - S[i - 1]);
  return V[10];
}

void battery_model_init(int percent) {
  emu_lock();
  B.soc = percent < 0 ? 0.8 : percent / 100.0;
  B.last_ms = emu_millis();
  emu_unlock();
}

/* advance the model; caller holds emu_lock */
static void step(void) {
  uint32_t now = emu_millis();
  double h = (now - B.last_ms) / 3600000.0 * (emu_opts.battery_speed > 0 ? emu_opts.battery_speed : 1.0);
  B.last_ms = now;
  double load = 0;
  if (emu_powered()) load = emu_sleeping() ? 2.6 : 53.0 + (lcd_model_backlight_on() ? 18.0 : 0.0);
  B.load_ma = load;
  double chg = 0;
  if (B.usb) {
    /* charger: CC 500 mA until 4.2 V, then CV taper; system load comes from USB (load sharing, Q1) */
    double v = ocv(B.soc);
    chg = v < 4.18 ? 500.0 : fmax(0.0, 500.0 * (1.0 - B.soc) / 0.1);
    if (chg < 50.0) chg = 0; /* termination at C/10 */
    B.soc += chg * h / CAP_MAH;
  } else {
    B.soc -= load * h / CAP_MAH;
  }
  B.chg_ma = chg;
  if (B.soc < 0) B.soc = 0;
  if (B.soc > 1) B.soc = 1;
}

void battery_model_read(battery_state_t *o) {
  emu_lock();
  step();
  o->percent = B.soc * 100.0;
  o->usb = B.usb;
  o->charging = B.usb && B.chg_ma > 0;
  o->current_ma = B.usb ? -B.chg_ma : B.load_ma;
  double v = B.usb ? (o->charging && ocv(B.soc) < 4.18 ? ocv(B.soc) + 0.5 * R_INT : ocv(B.soc)) : ocv(B.soc) - B.load_ma / 1000.0 * R_INT;
  o->vbat_mv = (uint16_t)lround(v * 1000.0);
  emu_unlock();
}

void battery_model_usb(bool plugged) {
  emu_lock();
  step();
  B.usb = plugged;
  emu_unlock();
  emu_logf("USB-C %s", plugged ? "plugged in: charging BT1" : "unplugged: running from BT1");
  emu_request_repaint();
}

bool battery_model_usb_plugged(void) { return B.usb; }

/* the firmware's view: ADC + CHG_STAT + VBUS sense */
void hal_power(hal_power_t *out) {
  battery_state_t s;
  battery_model_read(&s);
  out->vbat_mv = s.vbat_mv;
  out->charging = s.charging;
  out->usb = s.usb;
}
