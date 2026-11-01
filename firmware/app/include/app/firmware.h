#ifndef APP_FIRMWARE_H
#define APP_FIRMWARE_H

/* Firmware entry points: the platform's main() calls fw_init() once, then
   fw_poll() forever (hal_idle() in between). */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t idle_lock_ms;  /* auto-lock after this long without input (default 5 min) */
  uint32_t pin_delay_ms;  /* base delay after the 3rd wrong PIN, doubling (default 1 s) */
} fw_config_t;

extern fw_config_t fw_config;

void fw_init(void);
void fw_poll(void);

/* Lock now (wipes seeds from RAM) and tell the app. */
void fw_lock(const char *reason);
/* A request arrived (keeps the auto-lock timer from firing mid-session). */
void fw_activity(void);
/* The device was wiped / created / unlocked: protocol state must follow. */
void fw_state_changed(void);

#define FW_MODEL "tredder"
#ifndef FW_VERSION
#define FW_VERSION "0.2.0"
#endif

#endif
