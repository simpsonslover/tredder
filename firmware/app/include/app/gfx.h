#ifndef APP_GFX_H
#define APP_GFX_H

/* Framebuffer graphics for the ST7789 panel, used in landscape (320x240, RGB565):
   the 2.0" module lies across the board with its pin header on J4 at the left. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GFX_W 320
#define GFX_H 240

typedef struct {
  uint16_t cp;           /* Unicode code point */
  uint8_t w, h;          /* bitmap size */
  int8_t x0, y0;         /* offset from pen position / line top */
  uint8_t adv;           /* advance width */
  uint32_t off;          /* into bits[] (4 bpp, row-major, high nibble first) */
} wl_glyph_t;

typedef struct {
  uint8_t height, ascent;
  uint16_t count;
  const wl_glyph_t *glyphs;
  const uint8_t *bits;
} wl_font_t;

extern const wl_font_t wl_font_body, wl_font_bold, wl_font_title, wl_font_big;

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

void gfx_init(void);              /* power the module, ST7789 init sequence + clear */
void gfx_sleep(void);             /* backlight off, SLPIN, module unpowered (sleep mode) */
void gfx_fill(int x, int y, int w, int h, uint16_t color);
/* Draw UTF-8 text; returns the x after the last glyph. */
int gfx_text(int x, int y, const char *utf8, const wl_font_t *f, uint16_t color);
int gfx_text_width(const char *utf8, const wl_font_t *f);
void gfx_flush(void);             /* push the framebuffer to the panel */
void gfx_backlight(bool on);
void gfx_resync(void);            /* re-send panel settings (recovers from a corrupted command) */
void gfx_invalidate(void);        /* next gfx_flush repaints the whole screen */

#endif
