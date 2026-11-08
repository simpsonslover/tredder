/*
 * The wallet firmware (app/: UI, vault, PIN, protocol) on the RP2350 board.
 * Same code as the PC emulator; only the HAL underneath differs.
 */
#include "app/firmware.h"
#include "app/hal.h"
#include "pico/stdlib.h"

bool hal_ble_init(void); /* hal_ble.c */

int main(void) {
  stdio_init_all();
  sleep_ms(500); /* lets a USB serial console catch the start-up log */
#ifdef TREDDER_DEMO_NO_SE
  hal_log("DEMO BUILD without the secure element: nothing is saved, seed import is disabled");
#endif
  if (!hal_ble_init()) hal_log("no Bluetooth: the phone app cannot connect");
  fw_init();
  for (;;) {
    fw_poll();
    hal_idle(5);
  }
}
