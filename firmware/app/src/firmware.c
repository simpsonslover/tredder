/*
 * Firmware main loop: buttons -> UI, radio -> protocol, auto-lock, sleep.
 *
 * Sleep: after fw_config.sleep_ms idle on the home/lock screen (or
 * flow_sleep_ms anywhere else) the device LOCKS (seeds wiped from RAM, any
 * flow abandoned), unpowers the display and the RP2350 goes dormant until a
 * button is pressed. Never while the app is connected or a request waits for
 * the user. The press that wakes it does nothing else.
 * Trade-off (security over convenience): waking always needs the PIN.
 * Identical on the RP2350 board and in the PC emulator.
 */
#include "app/firmware.h"

#include "app/gfx.h"
#include "app/hal.h"
#include "app/proto.h"
#include "app/rng.h"
#include "app/se.h"
#include "app/ui.h"
#include "app/vault.h"

fw_config_t fw_config = {5u * 60u * 1000u, 1000u, 60u * 1000u, 10u * 60u * 1000u};

#define POWER_POLL_MS 2000u

#define DEBOUNCE_MS 15u
#define REPEAT_DELAY_MS 450u
#define REPEAT_MS 110u

static struct {
  uint8_t stable, last_raw;
  uint32_t changed_at, repeat_at;
  uint32_t last_activity;
  uint32_t session;
  bool connected;
  bool wake_hold; /* woke from sleep: ignore buttons until all are released */
  uint32_t power_at;
  bool low_warned;
  uint32_t low_since;
} M;

int fw_battery_percent(uint16_t mv) {
  /* 1S LiPo open-circuit curve (typical), linear between points */
  static const uint16_t MV[] = {3300, 3500, 3600, 3650, 3700, 3750, 3800, 3900, 4000, 4100, 4200};
  static const uint8_t PCT[] = {0, 5, 12, 20, 30, 40, 50, 65, 78, 90, 100};
  if (!mv) return -1;
  if (mv <= MV[0]) return 0;
  for (unsigned i = 1; i < sizeof(MV) / sizeof(MV[0]); i++)
    if (mv <= MV[i]) return PCT[i - 1] + (int)((uint32_t)(mv - MV[i - 1]) * (PCT[i] - PCT[i - 1]) / (MV[i] - MV[i - 1]));
  return 100;
}

void fw_state_changed(void) {
  if (!vault_initialized()) proto_forget_pairing();
}

void fw_lock(const char *reason) {
  bool was = vault_unlocked();
  vault_lock();
  if (was) hal_log("device locked (%s)", reason);
  proto_locked();
  ui_home();
}

_Noreturn void rng_fatal(const char *why) {
  hal_log("FATAL: %s", why);
  vault_lock();
  gfx_fill(0, 0, GFX_W, GFX_H, RGB565(150, 20, 20));
  gfx_text(10, 20, "Hardware fault", &wl_font_title, 0xFFFF);
  gfx_text(10, 60, why, &wl_font_body, 0xFFFF);
  gfx_text(10, 90, "Device halted.", &wl_font_body, 0xFFFF);
  gfx_flush();
  for (;;) hal_idle(1000);
}

void fw_init(void) {
  gfx_init();
  if (se_init() != SE_OK) rng_fatal("secure element not responding");
  vault_init();
  proto_init();
  ui_init();
  M.last_activity = hal_millis();
  M.power_at = M.last_activity - POWER_POLL_MS;
  M.session = ~hal_link_session(); /* force a link check on the first poll: a phone may already be connected */
  hal_log("tredder firmware %s started (%s)", FW_VERSION, vault_initialized() ? "wallet present, locked" : "new device");
  ui_render();
}

static void buttons(uint32_t now) {
  uint8_t raw = hal_buttons_raw();
  if (M.wake_hold) { /* the press that woke the device is not an input */
    if (raw) return;
    M.wake_hold = false;
    M.stable = M.last_raw = 0;
    M.changed_at = now;
    return;
  }
  if (raw != M.last_raw) {
    M.last_raw = raw;
    M.changed_at = now;
    return;
  }
  if (now - M.changed_at < DEBOUNCE_MS) return;
  uint8_t pressed = (uint8_t)(raw & ~M.stable);
  M.stable = raw;
  for (uint8_t b = 1; b && b <= HAL_BTN_DOWN; b <<= 1) {
    if (pressed & b) {
      M.last_activity = now;
      M.repeat_at = now + REPEAT_DELAY_MS;
      ui_button(b);
    }
  }
  /* Up/Down auto-repeat while held (long lists, letter grid) */
  uint8_t held = raw & (HAL_BTN_UP | HAL_BTN_DOWN);
  if (held && !pressed && (int32_t)(now - M.repeat_at) >= 0) {
    M.repeat_at = now + REPEAT_MS;
    M.last_activity = now;
    ui_button(held & HAL_BTN_UP ? HAL_BTN_UP : HAL_BTN_DOWN);
  }
}

static void fw_sleep(const char *why) {
  hal_log("sleep (%s)", why);
  fw_lock("sleep");
  ui_render(); /* the lock screen is in the framebuffer for wake-up */
  gfx_sleep();
  hal_sleep();
  hal_log("woke up");
  M.wake_hold = true;
  M.low_warned = false;
  gfx_init(); /* the module was unpowered: full re-init + redraw */
  ui_redraw();
  M.last_activity = M.power_at = hal_millis();
}

/* Steady battery level. A LiPo's voltage dips while the radio transmits or the display redraws, and a
   breadboard adds noise: one reading at face value made the level jump (52 % -> 21 % -> 39 %). So:
   - the voltage is a running average (1/8 of each new reading, every 2 s: ~30 s to settle)
   - the shown percentage only moves when the average has moved by 3 % or more, and on battery it
     never goes up (a voltage rebound after a load is not charging); a change of charging state or
     of the battery being there restarts the average. */
/* Battery percent.
   On battery: from the voltage, a running average (1/8 per 2 s reading); after a 60 s settling window
   it only falls, in steps of 3 % or more.
   While charging the voltage says little: the charge current lifts it by 0.3-0.4 V on the breadboard
   (68 % shown while charging became 22 % on battery). So while charging the level is COUNTED instead:
   it starts from the last on-battery level and adds the charge put in, at the charger's current
   (FW_CHARGE_MA into FW_BATTERY_MAH), up to 99 %; 100 % once the voltage has reached the charger's
   end voltage (4.18 V) for a minute. Unplugging goes back to the voltage, settling for 60 s.
   (Proper answer: a fuel-gauge chip such as the MAX17048; this is the best a plain voltage divider
   can do.) */
static struct {
  uint32_t avg16;   /* on battery: mV x 16 */
  uint32_t raw16;   /* measured mV x 16, for the battery screen */
  uint32_t since;   /* when the state last changed */
  uint32_t last;    /* time of the previous reading */
  uint32_t milli;   /* while charging: level in 1/1000 % */
  uint32_t full_since;
  int shown;        /* -1 = none */
  int last_batt;    /* last settled on-battery level, -1 = unknown */
  bool charging, have;
} B = {.last_batt = -1};

static int steady_percent(const hal_power_t *p, uint32_t now) {
  if (!p->vbat_mv) {
    B.have = false;
    B.shown = -1;
    return -1;
  }
  uint32_t mv = p->vbat_mv;
  if (!B.have || p->charging != B.charging) { /* (re)start */
    if (B.have && !B.charging) B.last_batt = B.shown; /* keep the on-battery level for counting */
    B.avg16 = B.raw16 = mv * 16u;
    B.charging = p->charging;
    B.have = true;
    B.since = B.last = now;
    B.full_since = 0;
    if (p->charging) {
      /* start counting from the last on-battery level; unknown: a conservative voltage guess */
      int start = B.last_batt >= 0 ? B.last_batt : fw_battery_percent((uint16_t)(mv > 3400u ? mv - 350u : 3050u));
      B.milli = (uint32_t)start * 1000u;
      B.shown = start;
    } else {
      B.shown = fw_battery_percent((uint16_t)mv);
    }
    return B.shown;
  }
  uint32_t dt = now - B.last;
  B.last = now;
  B.raw16 = B.raw16 - B.raw16 / 8u + mv * 2u;
  if (p->charging) {
    /* charge in: FW_CHARGE_MA for dt ms, as a share of FW_BATTERY_MAH, in 1/1000 % */
    B.milli += (uint32_t)((uint64_t)FW_CHARGE_MA * dt * 100000ull / ((uint64_t)FW_BATTERY_MAH * 3600000ull));
    if (B.milli > 99000u) B.milli = 99000u;
    if (B.raw16 / 16u >= 4180u) { /* at the charger's end voltage */
      if (!B.full_since) B.full_since = now;
      if (now - B.full_since > 60000u) B.milli = 100000u;
    } else {
      B.full_since = 0;
    }
    B.shown = (int)(B.milli / 1000u);
    return B.shown;
  }
  B.avg16 = B.avg16 - B.avg16 / 8u + mv * 2u;
  int pct = fw_battery_percent((uint16_t)(B.avg16 / 16u));
  int d = pct - B.shown;
  bool settling = now - B.since < 60000u;
  if (settling ? (d >= 3 || d <= -3) : d <= -3) B.shown = pct;
  if (!settling) B.last_batt = B.shown;
  return B.shown;
}

static void power(uint32_t now) {
  if (now - M.power_at < POWER_POLL_MS) return;
  M.power_at = now;
  hal_power_t p;
  hal_power(&p);
  ui_set_battery(steady_percent(&p, now), p.charging);
  ui_set_battery_mv(p.vbat_mv ? (uint16_t)(B.raw16 / 16u) : 0);
  ui_set_battery_diag(p.vbat_state, p.raw_mv);
  ui_set_usb_power(p.usb);
  bool low = !p.usb && p.vbat_mv && p.vbat_mv < FW_BATTERY_LOW_MV;
  if (!low) {
    M.low_warned = false;
    return;
  }
  if (!M.low_warned) { /* warn once, then sleep shortly after unless plugged in */
    M.low_warned = true;
    M.low_since = now;
    hal_log("battery low (%u mV)", p.vbat_mv);
    if (!proto_waiting_user()) ui_battery_low();
  } else if (now - M.low_since > 10000u && !proto_waiting_user()) {
    fw_sleep("battery low");
  }
}

void fw_poll(void) {
  uint32_t now = hal_millis();
  power(now);
  ui_tick(now);
  buttons(now);

  uint32_t s = hal_link_session();
  if (s != M.session) {
    M.session = s;
    bool up = hal_link_connected();
    if (M.connected) { /* previous connection ended (possibly replaced by a new one) */
      hal_log("app disconnected");
      proto_link_changed(false);
      if (vault_unlocked()) fw_lock("app disconnected"); /* spec: lock when the phone goes away */
    }
    M.connected = up;
    if (up) {
      hal_log("app connected");
      proto_link_changed(true);
    }
  }
  proto_poll();

  /* Fresh time: handling a request above may have moved last_activity past `now`. */
  int32_t idle = (int32_t)(hal_millis() - M.last_activity);
  if (vault_unlocked() && idle > (int32_t)fw_config.idle_lock_ms) fw_lock("idle timeout");
  uint32_t limit = ui_idle() ? fw_config.sleep_ms : fw_config.flow_sleep_ms;
  if (fw_config.sleep_ms && !M.connected && !proto_waiting_user() && idle > (int32_t)limit) {
    fw_sleep("idle");
    return;
  }
  ui_render();
}

/* Requests count as activity too (an app session in progress). */
void fw_activity(void) { M.last_activity = hal_millis(); }
