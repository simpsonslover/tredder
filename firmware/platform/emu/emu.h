#ifndef EMU_H
#define EMU_H

/*
 * tredder emulator: runs the real firmware (firmware/app + firmware/core,
 * unmodified) on the PC against emulated peripherals:
 *   ST7789 panel    st7789_model.c  decodes the SPI command stream into GRAM
 *   buttons         GPIO levels set by the window / keyboard / robot
 *   NXP SE050       se_model.c      PIN counter, secret erase after 10 failures
 *   QSPI flash      flash_model.c   NOR semantics (erase to 0xFF, program clears bits)
 *   TRNG            OS CSPRNG raw bytes (health tests still run in firmware)
 *   BLE GATT        ws_link.c       WebSocket server carrying the same frames
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LCD_W 240 /* ST7789 native GRAM (portrait) */
#define LCD_H 320
#define GLASS_W 320 /* what you see: the panel lies landscape on the board */
#define GLASS_H 240

/* ---------------------------------------------------------------- options */
typedef struct {
  int port, debug_port;
  double scale; /* LCD pixel scale in the window, 0 = fit the screen */
  double battery_speed; /* battery model time multiplier (1 = real time) */
  int battery_pct;      /* initial state of charge, -1 = default */
  bool usb;             /* start with the USB-C cable plugged in */
  bool headless, ephemeral, auto_approve;
  char state_dir[512];
  const char *seed, *sweep_seed, *pin;
} emu_opts_t;
extern emu_opts_t emu_opts;

/* ---------------------------------------------------------------- shared peripheral state */
void emu_lock(void);
void emu_unlock(void);

/* buttons: GPIO levels (bit set = pressed) from several sources, OR-ed */
enum { SRC_MOUSE, SRC_KEYS, SRC_ROBOT, SRC_COUNT };
void emu_buttons_set(int src, uint8_t mask);
uint8_t emu_buttons(void);

/* ST7789 model */
void lcd_model_reset(void);
void lcd_model_cmd(uint8_t c);
void lcd_model_data(const uint8_t *d, size_t n);
void lcd_model_backlight(bool on);
void lcd_model_power(bool on); /* Q5: module supply; off loses GRAM and all settings */
bool lcd_model_backlight_on(void);
/* Copy what the glass shows, as mounted (after rotation / inversion / sleep / backlight), as 0x00RRGGBB. */
uint32_t lcd_model_snapshot(uint32_t *out /* GLASS_W*GLASS_H */);

/* BT1 LiPo + MCP73831 charger (battery_model.c) */
typedef struct {
  double percent, current_ma; /* current: + discharge, - charge */
  uint16_t vbat_mv;
  bool charging, usb;
} battery_state_t;
void battery_model_init(int percent);
void battery_model_read(battery_state_t *out);
void battery_model_usb(bool plugged);
bool battery_model_usb_plugged(void);

/* firmware sleep (hal_sleep): MCU dormant until a button */
bool emu_sleeping(void);

/* flash + secure element persistence */
void flash_model_load(void);
void se_model_load(void);
void sd_model_load(void);          /* sd_model.c: microSD card image */
void sd_model_insert(bool in);
bool sd_model_present(void);
uint8_t se_model_tries_left(void);

/* phone link */
bool ws_start(int port);
void ws_stop(void);
void ws_poll_io(int timeout_ms);
bool ws_connected(void);
const char *ws_peer(void);

/* debug link (tests): line-based commands on 127.0.0.1:debug_port */
bool dbg_start(int port);
void dbg_stop(void);
void dbg_poll(void);

/* robot finger: auto-approve (tests) + keyboard typing helper */
void robot_type(char c);
void robot_step(uint32_t now);
void robot_press(uint8_t btn);
bool robot_idle(void);

/* log ring for the window */
#define LOG_LINES 14
void emu_log_lines(char out[LOG_LINES][160], int *n);
void emu_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* SW1 power switch. OFF halts the MCU and drops the link; ON again restarts the
   process: RAM is lost, flash.bin and se050.bin keep their contents (a real power cycle). */
bool emu_powered(void);
void emu_power_switch(void);

/* window */
void win_run(void); /* GUI thread: never returns */
void emu_request_repaint(void);

/* platform helpers */
uint32_t emu_millis(void);
void emu_random(uint8_t *out, size_t n);
bool emu_read_file(const char *name, void *buf, size_t n);
bool emu_write_file(const char *name, const void *buf, size_t n);

#endif
