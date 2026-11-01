/*
 * Firmware main loop: buttons -> UI, radio -> protocol, auto-lock.
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

fw_config_t fw_config = {5u * 60u * 1000u, 1000u};

#define DEBOUNCE_MS 15u
#define REPEAT_DELAY_MS 450u
#define REPEAT_MS 110u

static struct {
  uint8_t stable, last_raw;
  uint32_t changed_at, repeat_at;
  uint32_t last_activity;
  uint32_t session;
  bool connected;
} M;

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
  M.session = ~hal_link_session(); /* force a link check on the first poll: a phone may already be connected */
  hal_log("tredder firmware %s started (%s)", FW_VERSION, vault_initialized() ? "wallet present, locked" : "new device");
  ui_render();
}

static void buttons(uint32_t now) {
  uint8_t raw = hal_buttons_raw();
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

void fw_poll(void) {
  uint32_t now = hal_millis();
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
  ui_render();
}

/* Requests count as activity too (an app session in progress). */
void fw_activity(void) { M.last_activity = hal_millis(); }
