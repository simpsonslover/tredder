/*
 * Bring-up test: battery measurement (breadboard or PCB). Shows on the display, every 2 s:
 *   - the voltage AT Pico pin 34 (GP28) and the battery voltage it means (x2, the R10/R11 divider)
 *   - GP28 read with the pad pull-up and pull-down: a floating pin follows the pull
 *   - a verdict in words (OK / pin 34 not connected / too high: R11 or its GND missing / too low)
 *   - USB power (VBUS, read through the radio chip), the charger STAT pin, the percentage
 *   - the last readings
 * Also printed on the USB serial console. Nothing secret involved: no wallet code at all.
 */
#include <stdio.h>
#include <string.h>

#include "app/gfx.h"
#include "board_pins.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#define BG RGB565(48, 48, 48)
#define BAR RGB565(66, 66, 66)
#define FG RGB565(255, 255, 255)
#define DIM RGB565(158, 158, 158)
#define ACC RGB565(129, 212, 250)
#define OK RGB565(128, 255, 128)
#define WARN RGB565(255, 238, 88)
#define BAD RGB565(255, 128, 128)

static int percent(unsigned mv) { /* same curve as the wallet (app/src/firmware.c) */
  static const unsigned MV[] = {3300, 3500, 3600, 3650, 3700, 3750, 3800, 3900, 4000, 4100, 4200};
  static const int PCT[] = {0, 5, 12, 20, 30, 40, 50, 65, 78, 90, 100};
  if (mv <= MV[0]) return 0;
  for (unsigned i = 1; i < 11; i++)
    if (mv <= MV[i]) return PCT[i - 1] + (int)((mv - MV[i - 1]) * (unsigned)(PCT[i] - PCT[i - 1]) / (MV[i] - MV[i - 1]));
  return 100;
}

static unsigned read_avg(void) {
  unsigned s = 0;
  for (int i = 0; i < 16; i++) s += adc_read();
  return s / 16;
}

static void line(int y, const char *k, const char *v, uint16_t c) {
  gfx_text(8, y, k, &wl_font_body, ACC);
  gfx_text(130, y, v, &wl_font_body, c);
}

int main(void) {
  stdio_init_all();
  sleep_ms(1500);
  gfx_init();
  bool radio = cyw43_arch_init() == 0; /* only to read VBUS (WL_GPIO 2) */
  adc_init();
  adc_gpio_init(PIN_VBAT_ADC);
  adc_select_input(BOARD_VBAT_ADC_INPUT);
  gpio_init(PIN_CHG_STAT);
  gpio_set_dir(PIN_CHG_STAT, GPIO_IN);
  gpio_pull_up(PIN_CHG_STAT);

  char hist[5][40] = {{0}};
  for (unsigned n = 1;; n++) {
    gpio_pull_up(PIN_VBAT_ADC);
    sleep_us(300);
    unsigned up = read_avg();
    gpio_pull_down(PIN_VBAT_ADC);
    sleep_us(300);
    unsigned down = read_avg();
    gpio_disable_pulls(PIN_VBAT_ADC);
    sleep_us(300);
    unsigned none = read_avg();
    unsigned pin_mv = (up + down) * 3300u / 2u / 4096u;     /* at GP28 */
    unsigned bat_mv = pin_mv * BOARD_VBAT_DIVIDER_NUM / BOARD_VBAT_DIVIDER_DEN;
    bool floating = up > down + 600u;
    bool high = !floating && (up + down) / 2 > 4000u;       /* ADC at full scale: >= 3.3 V at the pin */
    bool low = !floating && !high && bat_mv < 2500u;
    int vbus = radio ? cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN) : -1;
    bool stat_low = !gpio_get(PIN_CHG_STAT);

    const char *verdict = floating ? "NOT CONNECTED" : high ? "TOO HIGH" : low ? "TOO LOW" : "OK";
    const char *why1 = floating ? "Pin 34 floats: no wire," : high ? "Pin 34 gets >= 3.3 V:" : low ? "Battery < 2.5 V, or" : "Divider works.";
    const char *why2 = floating ? "or a loose one." : high ? "R11 or its GND missing" : low ? "R10 not connected" : "";

    char s[48];
    gfx_fill(0, 0, GFX_W, GFX_H, BG);
    gfx_fill(0, 0, GFX_W, 30, BAR);
    gfx_text(10, 5, "Battery test", &wl_font_title, FG);
    snprintf(s, sizeof(s), "#%u, every 2 s", n);
    gfx_text(GFX_W - 8 - gfx_text_width(s, &wl_font_body), 7, s, &wl_font_body, DIM);
    int y = 36;
    snprintf(s, sizeof(s), "%u.%02u V", pin_mv / 1000, pin_mv % 1000 / 10);
    line(y, "Pin 34 (GP28)", s, high ? BAD : FG), y += 18;
    snprintf(s, sizeof(s), "%u.%02u V  (%d%%)", bat_mv / 1000, bat_mv % 1000 / 10, floating || high ? 0 : percent(bat_mv));
    line(y, "Battery (x2)", floating || high ? "-" : s, FG), y += 18;
    snprintf(s, sizeof(s), "%u / %u / %u", up * 3300u / 4096u, down * 3300u / 4096u, none * 3300u / 4096u);
    line(y, "up/down/off mV", s, DIM), y += 18;
    line(y, "USB power", vbus < 0 ? "unknown" : vbus ? "yes (charging)" : "no (on battery)", vbus > 0 ? OK : FG), y += 18;
    line(y, "Charger STAT", stat_low ? "low (charging)" : "high / none", DIM), y += 22;
    uint16_t vc = floating || low ? WARN : high ? BAD : OK;
    gfx_round_rect(6, y - 2, GFX_W - 12, 42, 6, RGB565(62, 62, 62));
    gfx_text(14, y, verdict, &wl_font_bold, vc);
    gfx_text(14 + gfx_text_width(verdict, &wl_font_bold) + 10, y, why1, &wl_font_body, FG);
    gfx_text(14, y + 18, why2, &wl_font_body, FG);
    y += 46;

    memmove(hist[1], hist[0], sizeof(hist[0]) * 4);
    snprintf(hist[0], sizeof(hist[0]), "%3lus  pin %u.%02u V  %s", (unsigned long)(to_ms_since_boot(get_absolute_time()) / 1000),
             pin_mv / 1000, pin_mv % 1000 / 10, verdict);
    for (int i = 0; i < 4 && hist[i][0]; i++, y += 16) gfx_text(8, y, hist[i], &wl_font_body, i ? DIM : FG);
    gfx_flush();
    printf("#%u pin34 %u mV, battery %u mV, up %u down %u off %u, %s, vbus %d, stat %s\n", n, pin_mv, bat_mv, up, down, none,
           verdict, vbus, stat_low ? "low" : "high");
    sleep_ms(2000);
  }
}
