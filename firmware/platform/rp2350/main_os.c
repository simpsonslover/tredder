/*
 * The wallet firmware (app/: UI, vault, PIN, protocol) on the RP2350 board.
 * Same code as the PC emulator; only the HAL underneath differs.
 *
 * Start order: display and UI first, radio second, so the screen comes up even if the CYW43
 * radio is slow or fails.
 */
#include "app/firmware.h"
#include "app/gfx.h"
#include "app/hal.h"
#include "app/ui.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

bool hal_ble_init(void);                 /* hal_ble.c */
void hal_log_replay_if_connected(void);  /* hal_display.c */

int main(void) {
  /* Breadboard workaround, 2026-11-08: after a real power-up (USB plugged in) the display module
     ignored everything, even a direct full-screen fill, while the same code always worked after a
     warm reboot (flashing, watchdog). Ruled out on the board: the radio (no radio for 10 s: still
     black), the UI code (direct fill: black), pin/SPI configuration (register dumps identical cold
     and warm), timing (1.5 s delay: black), extra reset pulses and re-inits. Root cause not found.
     So: on a power-on reset, reboot warm once (power stays on) before touching any display pin.
     Costs ~0.3 s at power-up. The PCB switches the display supply itself (Q5): check it there
     WITHOUT this workaround before keeping it. */
  if (!watchdog_caused_reboot()) {
    sleep_ms(300);
    watchdog_reboot(0, 0, 10);
    for (;;) tight_loop_contents();
  }
  stdio_init_all();
  hal_log("boot");
#ifdef TREDDER_DEMO_NO_SE
  hal_log("DEMO BUILD without the secure element: nothing is saved, seed import is disabled");
#endif
  sleep_ms(250); /* the display module's own power-up and reset time */
  fw_init();
  hal_log("display + UI up, starting the radio");
  if (hal_ble_init()) hal_log("radio started");
  else hal_log("no Bluetooth: the phone app cannot connect");
  /* The radio's start-up current can disturb the display module on the breadboard: set the panel
     up again once the radio runs (one redraw at boot). */
  sleep_ms(100);
  gfx_init();
  ui_redraw();
  hal_log("display re-initialised after the radio start");
  for (;;) {
    fw_poll();
    hal_idle(5);
    hal_log_replay_if_connected();
  }
}
