/*
 * RP2350 HAL, first part: time, buttons, ST7789 display on SPI1, debug log.
 * (Flash, power, TRNG, sleep and the BLE link come with the full board port.)
 *
 * Pins from board_pins.h (generated from hardware/tools/design.py), so the same
 * code drives the PCB and the breadboard. On the breadboard LCD_PWR_N (GP18) and
 * LCD_BL_N (GP19) are not connected: the module and backlight are always on.
 */
#include <stdarg.h>
#include <stdio.h>

#include "app/hal.h"
#include "board_pins.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

/* 2 MHz for breadboard jumper wires: at 10 MHz a ringing clock edge on the breadboard corrupted
   bits, and a corrupted command byte can turn inversion off or start a scroll (seen on the
   breadboard: colour bars turned pink/purple and shifted). The ST7789 allows ~62 MHz writes, so the
   PCB can override this. A partial redraw of 40 rows takes ~100 ms at 2 MHz. */
#ifndef LCD_SPI_HZ
#define LCD_SPI_HZ (2u * 1000u * 1000u)
#endif

static bool lcd_pins_ready;

uint32_t hal_millis(void) { return to_ms_since_boot(get_absolute_time()); }

/* Weak: the wallet build replaces it with the radio-servicing version in hal_ble.c. */
__attribute__((weak)) void hal_idle(uint32_t max_ms) { sleep_ms(max_ms < 1 ? 0 : 1); }

static void out_pin(uint pin, bool level) {
  gpio_init(pin);
  gpio_put(pin, level);
  gpio_set_dir(pin, GPIO_OUT);
}

static void buttons_init(void) {
  static bool done;
  if (done) return;
  const uint pins[] = {PIN_BTN_BACK, PIN_BTN_CONFIRM, PIN_BTN_UP, PIN_BTN_DOWN};
  for (unsigned i = 0; i < 4; i++) {
    gpio_init(pins[i]);
    gpio_set_dir(pins[i], GPIO_IN);
    gpio_pull_up(pins[i]); /* required on the breadboard (no resistors); in parallel with R26-R29 on the PCB */
  }
  done = true;
}

uint8_t hal_buttons_raw(void) {
  buttons_init();
  uint8_t b = 0;
  if (!gpio_get(PIN_BTN_BACK)) b |= HAL_BTN_BACK;
  if (!gpio_get(PIN_BTN_CONFIRM)) b |= HAL_BTN_CONFIRM;
  if (!gpio_get(PIN_BTN_UP)) b |= HAL_BTN_UP;
  if (!gpio_get(PIN_BTN_DOWN)) b |= HAL_BTN_DOWN;
  return b;
}

static void lcd_pins_init(void) {
  if (lcd_pins_ready) return;
  out_pin(PIN_LCD_CS, 1);
  out_pin(PIN_LCD_DC, 1);
  out_pin(PIN_LCD_RST, 1);
  spi_init(BOARD_LCD_SPI, LCD_SPI_HZ);
  spi_set_format(BOARD_LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(PIN_LCD_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);
  lcd_pins_ready = true;
}

void hal_lcd_power(bool on) {
  if (on) {
    out_pin(PIN_LCD_PWR_N, 0); /* Q5 on */
    lcd_pins_init();
    return;
  }
  /* Drive every display pin low first: a pin left high back-powers the module (hardware/sim lcd_power). */
  if (lcd_pins_ready) spi_deinit(BOARD_LCD_SPI);
  const uint pins[] = {PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST, PIN_LCD_SCK, PIN_LCD_MOSI};
  for (unsigned i = 0; i < 5; i++) out_pin(pins[i], 0);
  lcd_pins_ready = false;
  out_pin(PIN_LCD_PWR_N, 1);
}

void hal_lcd_reset(void) {
  lcd_pins_init();
  gpio_put(PIN_LCD_RST, 0);
  sleep_ms(10);
  gpio_put(PIN_LCD_RST, 1);
  sleep_ms(120);
}

void hal_lcd_cmd(uint8_t cmd) {
  gpio_put(PIN_LCD_DC, 0);
  gpio_put(PIN_LCD_CS, 0);
  spi_write_blocking(BOARD_LCD_SPI, &cmd, 1); /* returns once the last bit is out */
  gpio_put(PIN_LCD_CS, 1);
}

void hal_lcd_data(const uint8_t *d, size_t n) {
  gpio_put(PIN_LCD_DC, 1);
  gpio_put(PIN_LCD_CS, 0);
  spi_write_blocking(BOARD_LCD_SPI, d, n);
  gpio_put(PIN_LCD_CS, 1);
}

void hal_lcd_backlight(bool on) { out_pin(PIN_LCD_BL_N, !on); /* Q4 is a PMOS: low = on */ }

void hal_log(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  printf("\n");
}
