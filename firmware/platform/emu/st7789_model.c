/*
 * Behavioural model of the Sitronix ST7789V as wired on the board (4-wire
 * SPI, 240x320 native, RGB565). Only the commands the firmware may send are
 * modelled; anything else is logged so driver mistakes show up.
 * The 2.0" IPS module inverts colours unless INVON (0x21) is sent, exactly
 * like the real panel, so a missing init step is visible in the emulator.
 *
 * GRAM is the panel's native 240x320 portrait array. MADCTL (0x36) decides how
 * CASET/RASET/RAMWR addresses map onto it (MY/MX mirror the row/column
 * counters, MV exchanges them; BGR swaps red and blue). The glass is mounted
 * landscape on the board (module header on J4 at the left, native top edge to
 * the right), so a driver that forgets the rotation shows a sideways picture,
 * as it would on the real board.
 */
#include <string.h>

#include "emu.h"

static struct {
  uint16_t gram[LCD_W * LCD_H];
  uint8_t cmd;
  uint8_t param[8];
  size_t nparam;
  bool sleeping, display_on, inverted, backlight, colmod_ok;
  uint8_t madctl;
  uint16_t xs, xe, ys, ye, x, y;
  uint8_t pixel_hi;
  bool have_hi;
  uint32_t frames;
} L;

void lcd_model_reset(void) {
  emu_lock();
  memset(&L, 0, sizeof(L));
  L.sleeping = true;
  L.xe = LCD_W - 1;
  L.ye = LCD_H - 1;
  for (size_t i = 0; i < LCD_W * LCD_H; i++) L.gram[i] = (uint16_t)(i * 2654435761u >> 16); /* power-up garbage */
  emu_unlock();
}

void lcd_model_backlight(bool on) {
  emu_lock();
  L.backlight = on;
  L.frames++;
  emu_unlock();
  emu_request_repaint();
}

static void apply_params(void) {
  switch (L.cmd) {
    case 0x3A: /* COLMOD */
      if (L.nparam == 1) {
        L.colmod_ok = (L.param[0] & 0x07) == 0x05;
        if (!L.colmod_ok) emu_logf("[st7789] COLMOD 0x%02x: only 16-bit RGB565 is wired", L.param[0]);
      }
      break;
    case 0x36: /* MADCTL */
      if (L.nparam == 1) L.madctl = L.param[0];
      break;
    case 0x2A: /* CASET */
      if (L.nparam == 4) L.xs = (uint16_t)(L.param[0] << 8 | L.param[1]), L.xe = (uint16_t)(L.param[2] << 8 | L.param[3]);
      break;
    case 0x2B: /* RASET */
      if (L.nparam == 4) L.ys = (uint16_t)(L.param[0] << 8 | L.param[1]), L.ye = (uint16_t)(L.param[2] << 8 | L.param[3]);
      break;
    default:
      break;
  }
}

void lcd_model_cmd(uint8_t c) {
  emu_lock();
  L.cmd = c;
  L.nparam = 0;
  L.have_hi = false;
  switch (c) {
    case 0x00: break;                                   /* NOP */
    case 0x01: /* SWRESET */
      L.sleeping = true, L.display_on = false, L.inverted = false;
      break;
    case 0x10: L.sleeping = true; break;                /* SLPIN */
    case 0x11: L.sleeping = false; break;               /* SLPOUT */
    case 0x13: break;                                   /* NORON */
    case 0x20: L.inverted = false; break;               /* INVOFF */
    case 0x21: L.inverted = true; break;                /* INVON */
    case 0x28: L.display_on = false; break;             /* DISPOFF */
    case 0x29: L.display_on = true; break;              /* DISPON */
    case 0x2C: L.x = L.xs, L.y = L.ys; break;           /* RAMWR */
    case 0x2A: case 0x2B: case 0x36: case 0x3A: break;  /* take parameters */
    default:
      emu_logf("[st7789] unmodelled command 0x%02x", c);
  }
  L.frames++;
  emu_unlock();
  emu_request_repaint();
}

void lcd_model_data(const uint8_t *d, size_t n) {
  emu_lock();
  if (L.cmd == 0x2C) {
    for (size_t i = 0; i < n; i++) {
      if (!L.have_hi) {
        L.pixel_hi = d[i];
        L.have_hi = true;
        continue;
      }
      L.have_hi = false;
      bool mv = L.madctl & 0x20;
      unsigned lw = mv ? LCD_H : LCD_W, lh = mv ? LCD_W : LCD_H; /* addressable columns x rows */
      if (L.x < lw && L.y < lh) {
        unsigned c = L.madctl & 0x40 ? lw - 1 - L.x : L.x, r = L.madctl & 0x80 ? lh - 1 - L.y : L.y;
        unsigned px = mv ? r : c, py = mv ? c : r;
        L.gram[py * LCD_W + px] = (uint16_t)(L.pixel_hi << 8 | d[i]);
      }
      if (++L.x > L.xe) {
        L.x = L.xs;
        if (++L.y > L.ye) L.y = L.ys;
      }
    }
  } else {
    for (size_t i = 0; i < n && L.nparam < sizeof(L.param); i++) L.param[L.nparam++] = d[i];
    apply_params();
  }
  L.frames++;
  emu_unlock();
  emu_request_repaint();
}

uint32_t lcd_model_snapshot(uint32_t *out) {
  emu_lock();
  bool visible = L.backlight && L.display_on && !L.sleeping;
  for (unsigned gy = 0; gy < GLASS_H; gy++) {
    for (unsigned gx = 0; gx < GLASS_W; gx++) {
      uint32_t *o = &out[gy * GLASS_W + gx];
      if (!visible) {
        *o = L.backlight && !L.display_on ? 0x101010 : 0;
        continue;
      }
      /* mounted landscape: glass (gx, gy) is native pixel (gy, 319 - gx) */
      uint16_t c = L.gram[(LCD_H - 1 - gx) * LCD_W + gy];
      if (!L.inverted) c = (uint16_t)~c; /* IPS panel: INVON shows true colours */
      uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
      if (L.madctl & 0x08) { uint32_t t = r; r = b; b = t; } /* BGR order on an RGB panel */
      *o = (r * 255 / 31) << 16 | (g * 255 / 63) << 8 | (b * 255 / 31);
    }
  }
  uint32_t f = L.frames;
  emu_unlock();
  return f;
}
