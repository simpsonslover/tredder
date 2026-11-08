/*
 * Bring-up test: display + buttons (works on the breadboard and the PCB; no
 * secure element, SD card or radio needed, and no secrets anywhere).
 *
 *   - colour bars: red, green, blue, white (wrong colours = inversion/RGB order)
 *   - a label in each corner (mirrored or upside-down picture = MADCTL)
 *   - one box per button that lights up while it is held
 *   - progress and button events on the USB serial console
 */
#include <stdio.h>

#include "app/gfx.h"
#include "app/hal.h"
#include "pico/stdlib.h"

#define WHITE RGB565(255, 255, 255)
#define GREY RGB565(90, 90, 90)
#define YELLOW RGB565(255, 210, 0)
#define BG RGB565(10, 14, 30)

static const struct { uint8_t bit; const char *name; } BTN[] = {
    {HAL_BTN_BACK, "BACK"}, {HAL_BTN_CONFIRM, "CONFIRM"}, {HAL_BTN_UP, "UP"}, {HAL_BTN_DOWN, "DOWN"}};

static void draw_buttons(uint8_t pressed) {
  for (int i = 0; i < 4; i++) {
    int x = 8 + i * 78, y = 178, on = pressed & BTN[i].bit;
    gfx_fill(x, y, 72, 40, on ? YELLOW : GREY);
    int w = gfx_text_width(BTN[i].name, &wl_font_body);
    gfx_text(x + (72 - w) / 2, y + 10, BTN[i].name, &wl_font_body, on ? BG : WHITE);
  }
}

/* Before the display is driven: each display pin as an input, first with the internal pull-down, then
   with the pull-up. A powered display input is high impedance and just follows the pull. A pin that
   stays low with the pull-up is held low (shorted to GND, or the display's VCC is missing so its input
   clamps drag the pin down); one that stays high with the pull-down is tied to 3.3 V or another pin.
   An open (loose) wire also just follows the pull, so this finds shorts and a dead VCC, not opens. */
static int pin_check(void) {
  static const struct { uint pin; const char *name; } P[] = {
      {20, "RST (GP20, pin 26)"}, {21, "CS  (GP21, pin 27)"}, {22, "DC  (GP22, pin 29)"},
      {26, "SCL (GP26, pin 31)"}, {27, "SDA (GP27, pin 32)"}};
  int bad = 0;
  printf("pin check (display pins as inputs):\n");
  for (unsigned i = 0; i < 5; i++) {
    gpio_init(P[i].pin);
    gpio_set_dir(P[i].pin, GPIO_IN);
    gpio_pull_down(P[i].pin);
    sleep_ms(5);
    int lo = gpio_get(P[i].pin);
    gpio_pull_up(P[i].pin);
    sleep_ms(5);
    int hi = gpio_get(P[i].pin);
    gpio_disable_pulls(P[i].pin);
    /* Held high? Drive it low for 20 us (2 mA drive) and read the pad back: a pull-up resistor on the
       module gives way (reads low), a wire touching 3.3 V does not (still reads high). */
    int strong = -1;
    if (lo && hi) {
      gpio_set_drive_strength(P[i].pin, GPIO_DRIVE_STRENGTH_2MA);
      gpio_put(P[i].pin, 0);
      gpio_set_dir(P[i].pin, GPIO_OUT);
      busy_wait_us(20);
      strong = gpio_get(P[i].pin);
      gpio_set_dir(P[i].pin, GPIO_IN);
      gpio_set_drive_strength(P[i].pin, GPIO_DRIVE_STRENGTH_4MA);
    }
    const char *v = !lo && hi ? "ok (follows the pull)"
                    : !hi && !lo ? "HELD LOW: shorted to GND, or the display has no VCC"
                    : strong == 1 ? "SHORTED HIGH: still high while the Pico drives it low (touching 3.3 V?)"
                    : strong == 0 ? "pulled high by a resistor (likely a pull-up on the display module: fine)"
                               : "odd (reads inverted)";
    bad += !(!lo && hi) && strong != 0;
    printf("  %s  %s\n", P[i].name, v);
  }
  return bad;
}

/* Live wire check, run between redraws (SPI idle, CS high): each display pin becomes an input with
   the internal pull-down for 50 us. The module's pull-up wins when the wire is connected (reads
   high); an open wire lets the pull-down win (reads low). Returns a bit per pin that reads open.
   CS is checked first and put back high before SCK/MOSI are released, so the display ignores the
   brief clock edges. Finds loose wires, which the start-up pin check cannot. */
static const struct { uint pin; bool spi; const char *name; } WIRE[] = {
    {21, false, "CS (Pico pin 27)"}, {22, false, "DC (Pico pin 29)"}, {20, false, "RST (Pico pin 26)"},
    {26, true, "SCL (Pico pin 31)"}, {27, true, "SDA (Pico pin 32)"}};

static uint8_t wires_open(void) {
  uint8_t open = 0;
  for (unsigned i = 0; i < 5; i++) {
    uint p = WIRE[i].pin;
    gpio_set_dir(p, GPIO_IN);
    gpio_set_function(p, GPIO_FUNC_SIO);
    gpio_set_dir(p, GPIO_IN);
    gpio_pull_down(p);
    busy_wait_us(50);
    if (!gpio_get(p)) open |= 1u << i;
    gpio_disable_pulls(p);
    if (WIRE[i].spi) {
      gpio_set_function(p, GPIO_FUNC_SPI);
    } else {
      gpio_put(p, 1); /* CS, DC and RST idle high */
      gpio_set_dir(p, GPIO_OUT);
    }
  }
  return open;
}

static void report_wires(void) {
  static uint8_t last;
  uint8_t o = wires_open();
  for (unsigned i = 0; i < 5; i++)
    if ((o ^ last) & (1u << i))
      printf("WIRE %s: %s\n", WIRE[i].name, o & (1u << i) ? "LOST CONTACT" : "connected again");
  last = o;
}

static void draw_test(void) {
  gfx_init();

  gfx_fill(0, 0, GFX_W, GFX_H, BG);
  const uint16_t bars[] = {RGB565(255, 0, 0), RGB565(0, 255, 0), RGB565(0, 0, 255), WHITE};
  const char *names[] = {"red", "green", "blue", "white"};
  for (int i = 0; i < 4; i++) {
    gfx_fill(i * 80, 40, 80, 50, bars[i]);
    gfx_text(i * 80 + 8, 96, names[i], &wl_font_body, WHITE);
  }
  gfx_text(8, 2, "TOP LEFT", &wl_font_body, YELLOW);
  gfx_text(GFX_W - 8 - gfx_text_width("TOP RIGHT", &wl_font_body), 2, "TOP RIGHT", &wl_font_body, YELLOW);
  gfx_text(8, 128, "tredder display test", &wl_font_title, WHITE);
  gfx_text(8, 156, "Hold a button: its box lights up", &wl_font_body, WHITE);
  draw_buttons(0);
  gfx_flush();
}

int main(void) {
  stdio_init_all();
  sleep_ms(1500);
  printf("\n== display + buttons bring-up ==\n");
  int bad = pin_check();
  printf(bad ? "pin check: %d problem(s) above\n" : "pin check: no shorts found (a loose wire can't be seen this way)\n", bad);

  printf("display: init (SPI1, SCK GP26, MOSI GP27, CS GP21, DC GP22, RST GP20)\n");
  draw_test();
  printf("display: test picture drawn. Colour bars left to right should read red, green, blue, white,\n"
         "         with TOP LEFT / TOP RIGHT in the top corners.\n");

  /* Debounce: a new button state counts only after it has read the same for 30 ms, so one press
     redraws the box once instead of once per contact bounce. */
  /* Heartbeat: once a second the counter top-centre goes up and "alive" is printed. Counter stops
     but serial goes on = the display is stuck; both stop = the Pico itself has hung. Every 5 s the
     panel settings are re-sent and the whole picture repainted, so a stuck display recovers. */
  uint8_t last = 0xFF, cand = 0xFF;
  int stable = 0;
  uint32_t beat = 0, next_beat = hal_millis() + 1000;
  for (;;) {
    if ((int32_t)(hal_millis() - next_beat) >= 0) {
      next_beat += 1000;
      beat++;
      char s[16];
      snprintf(s, sizeof s, "%lu", (unsigned long)beat);
      gfx_fill(130, 0, 60, 24, BG);
      gfx_text(160 - gfx_text_width(s, &wl_font_body) / 2, 2, s, &wl_font_body, WHITE);
      if (beat % 5 == 0) gfx_invalidate();
      gfx_resync();
      gfx_flush();
      printf("alive %lu\n", (unsigned long)beat);
    }
    report_wires();
    uint8_t raw = hal_buttons_raw();
    stable = raw == cand ? stable + 1 : 0;
    cand = raw;
    sleep_ms(10);
    if (stable < 3) continue;
    uint8_t b = cand;
    if (b != last) {
      draw_buttons(b);
      gfx_resync();
      gfx_flush();
      printf("buttons:");
      for (int i = 0; i < 4; i++)
        if (b & BTN[i].bit) printf(" %s", BTN[i].name);
      printf(b ? "\n" : " (none)\n");
      last = b;
    }
  }
}
