#ifndef APP_HAL_H
#define APP_HAL_H

/*
 * Hardware abstraction: everything the firmware application needs from the
 * board. Implemented twice:
 *   platform/rp2350/  the real Pico 2 W board (GPIO, SPI1 ST7789, flash, TRNG, BLE)
 *   platform/emu/     the PC emulator (emulated ST7789, GPIO buttons, flash file,
 *                     SE050 model, WebSocket instead of BLE)
 * The application code above this line is identical on both.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------- time */
uint32_t hal_millis(void);
/* Sleep until an input event (button edge, radio data) or max_ms elapses. */
void hal_idle(uint32_t max_ms);

/* ---------------------------------------------------------------- buttons
   Raw pin levels already inverted (bit set = pressed). Active-low GPIO 12..15.
   The HAL enables the internal pull-ups: the breadboard has no resistors, the PCB
   adds 10 k pull-ups and RC debounce. Firmware debounces again. */
#define HAL_BTN_BACK 0x01u
#define HAL_BTN_CONFIRM 0x02u
#define HAL_BTN_UP 0x04u
#define HAL_BTN_DOWN 0x08u
uint8_t hal_buttons_raw(void);

/* ---------------------------------------------------------------- ST7789 on SPI1 */
void hal_lcd_reset(void);                         /* pulse LCD_RST low, wait 120 ms */
void hal_lcd_cmd(uint8_t cmd);                    /* DC low, CS low, 1 byte */
void hal_lcd_data(const uint8_t *d, size_t n);    /* DC high, CS low, n bytes */
void hal_lcd_backlight(bool on);                  /* LCD_BL_N via Q4 */
/* Display module supply (LCD_PWR_N on GP18 via Q5). Before switching OFF the HAL
   drives RST/CS/DC/SCK/MOSI LOW: a pin left high back-powers the unpowered
   module to ~2.5 V through its ESD diodes (hardware/sim: lcd_power). After ON,
   LCD_VCC needs ~1.3 ms; gfx_init() waits before talking to the panel. */
void hal_lcd_power(bool on);

/* ---------------------------------------------------------------- power
   VBAT from GP28 / ADC2 (divider R10/R11, per-device gain calibration),
   charging from CHG_STAT (GP5, low = charging), USB from the CYW43 VBUS sense. */
typedef enum {
  HAL_VBAT_OK = 0,
  HAL_VBAT_NOT_CONNECTED, /* GP28 floats: no divider (USB-only build) */
  HAL_VBAT_TOO_HIGH,      /* > 4.6 V: GP28 sees the whole battery (R11 / its GND missing) */
  HAL_VBAT_TOO_LOW,       /* < 2.5 V: no cell, or the divider's top (R10) is not connected */
} hal_vbat_state_t;

typedef struct {
  uint16_t vbat_mv; /* 0 = not measured */
  bool charging;
  bool usb;
  uint8_t vbat_state; /* hal_vbat_state_t: why vbat_mv is 0 */
  uint16_t raw_mv;    /* what GP28 read, scaled as if the divider were right (diagnostics) */
} hal_power_t;
void hal_power(hal_power_t *out);

/* Sleep: display + SE + SD already off; radio off; the RP2350 goes DORMANT and
   wakes on any button edge (GPIO 12..15). RAM is kept. Returns after wake-up. */
void hal_sleep(void);

/* ---------------------------------------------------------------- entropy
   Raw samples from the RP2350 TRNG (unconditioned: health tests run in core). */
void hal_trng_raw(uint8_t *out, size_t n);

/* ---------------------------------------------------------------- storage flash
   A dedicated region of the QSPI flash, HAL_STORAGE_SECTORS x 4 KiB. */
#define HAL_FLASH_SECTOR 4096u
#define HAL_STORAGE_SECTORS 2u
bool hal_flash_read(uint32_t offset, void *buf, size_t n);
bool hal_flash_erase(uint32_t sector);
bool hal_flash_program(uint32_t offset, const void *buf, size_t n);

/* ---------------------------------------------------------------- phone link
   On the device: BLE GATT (RX write / TX notify, one bonded phone).
   One frame = one GATT write or notification (protocol/PROTOCOL.md section 2). */
bool hal_link_connected(void);
/* Increments on every connect and disconnect. */
uint32_t hal_link_session(void);
size_t hal_link_frame_size(void);
/* Copy one received frame into buf. Returns its length, 0 if none waiting. */
size_t hal_link_recv(uint8_t *buf, size_t max);
bool hal_link_send(const uint8_t *frame, size_t n);

/* ---------------------------------------------------------------- diagnostics
   Debug console. Production builds compile this to nothing. Never log secrets. */
void hal_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* The newest log text ('\n'-separated lines, oldest first), for the on-device log screen. Returns
   its length; out is NUL-terminated. The log never holds secrets (rule above). */
size_t hal_log_recent(char *out, size_t max);

#endif
