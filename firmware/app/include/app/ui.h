#ifndef APP_UI_H
#define APP_UI_H

/* Screens and on-device flows (setup, unlock, import, approvals). */

#include <stdbool.h>
#include <stdint.h>

#include "wallet/review.h"

void ui_init(void);
void ui_home(void);                 /* back to the top-level screen for the current state */
bool ui_idle(void);                 /* on a top-level screen (requests may interrupt) */
void ui_button(uint8_t hal_btn);    /* one debounced press */
void ui_tick(uint32_t now_ms);
void ui_render(void);               /* redraw if something changed */

/* Protocol approvals. The review must stay valid until done() is called. */
typedef void (*ui_done_fn)(bool approved);
void ui_prompt(const wl_review_t *rv, bool need_pin, ui_done_fn done);
void ui_cancel_prompt(void);
void ui_set_status(bool app_connected, bool paired);
/* Battery icon in the title bar: percent 0..100 or -1 (unknown / no cell). */
void ui_set_battery(int percent, bool charging);
void ui_set_battery_mv(uint16_t mv); /* battery screen: 0 = no battery */
/* Why there is no reading (hal_vbat_state_t) and the raw value: shown on the battery screen, so a
   wiring fault can be found without a PC. */
void ui_set_battery_diag(uint8_t state, uint16_t raw_mv);
/* "Battery low" warning page (the firmware sleeps shortly after). */
void ui_battery_low(void);
/* Redraw everything (after the display was unpowered in sleep). */
void ui_redraw(void);
/* Total value as the phone app reports it (main page; display only, the device cannot verify it).
   NULL / "" clears it. */
void ui_set_balance(const char *text);

#ifdef WL_DEBUGLINK
#if defined(__arm__) || defined(__riscv)
#error "WL_DEBUGLINK exposes the screen to a PC: never build it into device firmware"
#endif
/* EMULATOR/TEST BUILDS ONLY: what is on screen, as text (never in device firmware). */
typedef struct {
  const char *kind; /* "menu" "pages" "pin" "word" */
  const char *title;
  uint8_t nlines, nitems, cursor, page, npages;
  const char *lines[16];
  const char *items[32];
} ui_debug_t;
void ui_debug(ui_debug_t *out);
#endif

#endif
