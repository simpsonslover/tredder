/*
 * ST7789 driver (2.0" 240x320 IPS module on J4, SPI1, mounted landscape) +
 * framebuffer drawing.
 * The panel gets real ST7789 command sequences through hal_lcd_cmd/data; the
 * emulator decodes the same byte stream.
 */
#include "app/gfx.h"

#include <string.h>

#include "app/hal.h"

/* ST7789 commands */
#define ST_SWRESET 0x01
#define ST_SLPIN 0x10
#define ST_SLPOUT 0x11
#define ST_NORON 0x13
#define ST_INVON 0x21
#define ST_DISPOFF 0x28
#define ST_DISPON 0x29
#define ST_CASET 0x2A
#define ST_RASET 0x2B
#define ST_RAMWR 0x2C
#define ST_MADCTL 0x36
#define ST_VSCSAD 0x37
#define ST_COLMOD 0x3A

static uint16_t fb[GFX_W * GFX_H];
static int dirty_y0 = GFX_H, dirty_y1 = -1;

static void delay_ms(uint32_t ms) {
  uint32_t t0 = hal_millis();
  while (hal_millis() - t0 < ms) hal_idle(ms);
}

static void cmd_data(uint8_t c, const uint8_t *d, size_t n) {
  hal_lcd_cmd(c);
  if (n) hal_lcd_data(d, n);
}

void gfx_init(void) {
  hal_lcd_power(true);
  delay_ms(5); /* LCD_VCC soft start through Q5 (~1.3 ms to 90 %) */
  hal_lcd_reset();
  cmd_data(ST_SWRESET, NULL, 0);
  delay_ms(150);
  cmd_data(ST_SLPOUT, NULL, 0);
  delay_ms(10);
  const uint8_t colmod = 0x55; /* 16 bit/pixel RGB565 */
  cmd_data(ST_COLMOD, &colmod, 1);
  /* MV|MX: row/column exchange + column mirror = the 240x320 panel addressed as 320x240
     landscape, upright with the module's pin header (J4) on the left. ASSUMPTION until the
     module is in hand: if the picture is upside down on it, use 0xA0 (MV|MY). */
  const uint8_t madctl = 0x60;
  cmd_data(ST_MADCTL, &madctl, 1);
  cmd_data(ST_INVON, NULL, 0); /* IPS panels need inversion for correct colours */
  cmd_data(ST_NORON, NULL, 0);
  memset(fb, 0, sizeof(fb));
  dirty_y0 = 0, dirty_y1 = GFX_H - 1;
  gfx_flush();
  cmd_data(ST_DISPON, NULL, 0);
  gfx_backlight(true);
}

void gfx_backlight(bool on) { hal_lcd_backlight(on); }

/* Re-send the panel settings (no reset, no blanking). A corrupted SPI byte can land as a stray
   command: inversion off (wrong colours), a scroll (picture shifted) or sleep-in (frozen picture).
   Seen on the breadboard; this undoes every one of those, so call it before redraws. */
void gfx_resync(void) {
  cmd_data(ST_SLPOUT, NULL, 0); /* no effect when already awake */
  delay_ms(5);
  const uint8_t colmod = 0x55, madctl = 0x60, vsa[2] = {0, 0};
  cmd_data(ST_COLMOD, &colmod, 1);
  cmd_data(ST_MADCTL, &madctl, 1);
  cmd_data(ST_VSCSAD, vsa, 2); /* scroll start back to 0 */
  cmd_data(ST_INVON, NULL, 0);
  cmd_data(ST_NORON, NULL, 0); /* leaves partial / scroll mode */
  cmd_data(ST_DISPON, NULL, 0);
}

void gfx_invalidate(void) { dirty_y0 = 0, dirty_y1 = GFX_H - 1; }

void gfx_sleep(void) {
  gfx_backlight(false);
  cmd_data(ST_DISPOFF, NULL, 0);
  cmd_data(ST_SLPIN, NULL, 0);
  delay_ms(5);
  hal_lcd_power(false); /* GRAM is lost: the next gfx_init() redraws everything */
}

static void mark(int y0, int y1) {
  if (y0 < dirty_y0) dirty_y0 = y0;
  if (y1 > dirty_y1) dirty_y1 = y1;
}

void gfx_fill(int x, int y, int w, int h, uint16_t color) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > GFX_W) w = GFX_W - x;
  if (y + h > GFX_H) h = GFX_H - y;
  if (w <= 0 || h <= 0) return;
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) fb[yy * GFX_W + xx] = color;
  mark(y, y + h - 1);
}

/* Decode one UTF-8 code point. */
static uint32_t utf8_next(const char **s) {
  const uint8_t *p = (const uint8_t *)*s;
  uint32_t cp;
  int n;
  if (p[0] < 0x80) cp = p[0], n = 1;
  else if ((p[0] & 0xE0) == 0xC0) cp = p[0] & 0x1F, n = 2;
  else if ((p[0] & 0xF0) == 0xE0) cp = p[0] & 0x0F, n = 3;
  else cp = p[0] & 0x07, n = 4;
  for (int i = 1; i < n; i++) {
    if ((p[i] & 0xC0) != 0x80) {
      *s += i;
      return '?';
    }
    cp = cp << 6 | (p[i] & 0x3F);
  }
  *s += n;
  return cp;
}

static const wl_glyph_t *glyph(const wl_font_t *f, uint32_t cp) {
  for (uint16_t i = 0; i < f->count; i++)
    if (f->glyphs[i].cp == cp) return &f->glyphs[i];
  for (uint16_t i = 0; i < f->count; i++)
    if (f->glyphs[i].cp == '?') return &f->glyphs[i];
  return NULL;
}

static uint16_t blend(uint16_t bg, uint16_t fg, unsigned a /* 0..15 */) {
  unsigned rb = bg >> 11, gb = (bg >> 5) & 63, bb = bg & 31;
  unsigned rf = fg >> 11, gf = (fg >> 5) & 63, bf = fg & 31;
  unsigned r = (rf * a + rb * (15 - a)) / 15, g = (gf * a + gb * (15 - a)) / 15, b = (bf * a + bb * (15 - a)) / 15;
  return (uint16_t)(r << 11 | g << 5 | b);
}

int gfx_text(int x, int y, const char *s, const wl_font_t *f, uint16_t color) {
  while (*s) {
    const wl_glyph_t *g = glyph(f, utf8_next(&s));
    if (!g) continue;
    for (int gy = 0; gy < g->h; gy++) {
      int py = y + g->y0 + gy;
      if (py < 0 || py >= GFX_H) continue;
      for (int gx = 0; gx < g->w; gx++) {
        int px = x + g->x0 + gx;
        if (px < 0 || px >= GFX_W) continue;
        uint32_t bit = (uint32_t)gy * g->w + (uint32_t)gx;
        uint8_t byte = f->bits[g->off + bit / 2];
        unsigned a = bit & 1 ? byte & 15 : byte >> 4;
        if (a) fb[py * GFX_W + px] = a == 15 ? color : blend(fb[py * GFX_W + px], color, a);
      }
    }
    mark(y, y + f->height);
    x += g->adv;
  }
  return x;
}

int gfx_text_width(const char *s, const wl_font_t *f) {
  int w = 0;
  while (*s) {
    const wl_glyph_t *g = glyph(f, utf8_next(&s));
    if (g) w += g->adv;
  }
  return w;
}

void gfx_flush(void) {
  if (dirty_y1 < dirty_y0) return;
  if (dirty_y1 >= GFX_H) dirty_y1 = GFX_H - 1;
  const uint8_t caset[4] = {0, 0, (GFX_W - 1) >> 8, (GFX_W - 1) & 0xFF};
  const uint8_t raset[4] = {(uint8_t)(dirty_y0 >> 8), (uint8_t)dirty_y0, (uint8_t)(dirty_y1 >> 8), (uint8_t)dirty_y1};
  cmd_data(ST_CASET, caset, 4);
  cmd_data(ST_RASET, raset, 4);
  hal_lcd_cmd(ST_RAMWR);
  /* Big-endian RGB565 over SPI, one row at a time (DMA-sized chunks on the device). */
  uint8_t row[GFX_W * 2];
  for (int y = dirty_y0; y <= dirty_y1; y++) {
    for (int x = 0; x < GFX_W; x++) {
      uint16_t c = fb[y * GFX_W + x];
      row[2 * x] = (uint8_t)(c >> 8);
      row[2 * x + 1] = (uint8_t)c;
    }
    hal_lcd_data(row, sizeof(row));
  }
  dirty_y0 = GFX_H, dirty_y1 = -1;
}
