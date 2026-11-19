#ifndef APP_FIRMWARE_H
#define APP_FIRMWARE_H

/* Firmware entry points: the platform's main() calls fw_init() once, then
   fw_poll() forever (hal_idle() in between). */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t idle_lock_ms;  /* auto-lock after this long without input (default 5 min) */
  uint32_t pin_delay_ms;  /* base delay after the 3rd wrong PIN, doubling (default 1 s) */
  uint32_t sleep_ms;      /* sleep after this long idle on the home / lock screen (default 60 s, 0 = never) */
  uint32_t flow_sleep_ms; /* ... or anywhere else, abandoning the flow (default 10 min) */
} fw_config_t;

extern fw_config_t fw_config;

void fw_init(void);
void fw_poll(void);

/* Lock now (wipes seeds from RAM) and tell the app. */
void fw_lock(const char *reason);
/* Battery level 0..100 from the cell voltage (1S LiPo, light load), -1 if unknown. */
int fw_battery_percent(uint16_t mv);
#define FW_BATTERY_LOW_MV 3350u /* below this on battery: warn, then sleep */
/* For counting the charge while charging (firmware.c): the battery and the charger's current.
   Breadboard: 1100 mAh cell, TP4056 module at 1 A (R3 1.2 k). PCB: 500 mAh cell, MCP73831 (set the
   real values for that build). */
#ifndef FW_BATTERY_MAH
#define FW_BATTERY_MAH 1100u
#endif
#ifndef FW_CHARGE_MA
#define FW_CHARGE_MA 1000u
#endif

/* A request arrived (keeps the auto-lock timer from firing mid-session). */
void fw_activity(void);
/* The device was wiped / created / unlocked: protocol state must follow. */
void fw_state_changed(void);

#define FW_MODEL "tredder"
#ifndef FW_VERSION
#define FW_VERSION "0.2.0"
#endif

#endif
