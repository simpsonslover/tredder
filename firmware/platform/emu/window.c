/*
 * Win32 window: the rev A board, drawn to scale from hardware/tools/gen_pcb.py
 * (85.6 x 54 mm, top view), with the emulated ST7789 glass in the display
 * module's active area. The buttons sit where SW2-SW5 are on the PCB, and
 * SW1 is the power switch. A status panel and the log are on the right.
 *
 * Board geometry below is in mm, origin top-left (same frame as gen_pcb.py's
 * PLACE table). Keep it in sync if the layout changes.
 */
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "app/firmware.h"
#include "app/hal.h"
#include "emu.h"

#define DESK RGB(22, 24, 28)
#define MASK RGB(54, 64, 40)      /* solder mask (olive, as in the KiCad render) */
#define MASK_EDGE RGB(34, 40, 26)
#define SILK RGB(236, 236, 226)
#define GOLD RGB(214, 178, 72)
#define SILVER RGB(196, 198, 204)
#define STEEL RGB(150, 154, 162)
#define PART RGB(26, 26, 28)
#define PANEL_TXT RGB(220, 225, 232)
#define DIM_TXT RGB(140, 148, 160)

#define BW 85.6
#define BH 54.0

static HWND hwnd;
static volatile LONG repaint;
static uint32_t pixels[GLASS_W * GLASS_H];
static uint32_t last_frame = 0xFFFFFFFF;
static uint8_t keys_mask;
static double K;   /* pixels per mm */
static int OXp, OYp; /* board origin in the window */
static RECT panel_rc;
static int win_w, win_h;
static HFONT f_title, f_text, f_mono, f_silk, f_silk_s, f_silk_v;

/* buttons on the PCB: reference, position (mm), silk label + its position */
static const struct {
  const char *ref;
  double x, y;
  uint8_t bit;
  const char *label;
  double lx, ly;
} BTN[4] = {{"SW2", 76.0, 37.0, HAL_BTN_UP, "UP", 76.0, 33.2},
            {"SW3", 76.0, BH - 6.0, HAL_BTN_DOWN, "DOWN", 76.0, BH - 1.6},
            {"SW4", 53.0, BH - 6.0, HAL_BTN_BACK, "BACK", 53.0, BH - 1.6},
            {"SW5", 65.0, BH - 6.0, HAL_BTN_CONFIRM, "CONFIRM", 65.0, BH - 1.6}};
static const double SW1_X = 29.0, SW1_Y = BH - 3.5;
/* active area of the 2.0" panel (gen_lcd_model.py): 40.8 x 30.6 mm centred at (33.25, 20) */
static const double LCD_X0 = 12.85, LCD_Y0 = 4.7, LCD_WMM = 40.8, LCD_HMM = 30.6;

void emu_request_repaint(void) { InterlockedExchange(&repaint, 1); }

/* ---------------------------------------------------------------- mm -> pixels */
static int X(double mm) { return OXp + (int)lround(mm * K); }
static int Y(double mm) { return OYp + (int)lround(mm * K); }
static int S(double mm) { int v = (int)lround(mm * K); return v < 1 ? 1 : v; }

static void rect(HDC dc, double x0, double y0, double x1, double y1, COLORREF fill, COLORREF edge) {
  HBRUSH b = CreateSolidBrush(fill);
  HPEN p = CreatePen(PS_SOLID, 1, edge);
  HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
  Rectangle(dc, X(x0), Y(y0), X(x1) + 1, Y(y1) + 1);
  SelectObject(dc, ob), SelectObject(dc, op);
  DeleteObject(b), DeleteObject(p);
}
static void rrect(HDC dc, double x0, double y0, double x1, double y1, double r, COLORREF fill, COLORREF edge, int pen) {
  HBRUSH b = CreateSolidBrush(fill);
  HPEN p = CreatePen(PS_SOLID, pen, edge);
  HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
  RoundRect(dc, X(x0), Y(y0), X(x1) + 1, Y(y1) + 1, S(2 * r), S(2 * r));
  SelectObject(dc, ob), SelectObject(dc, op);
  DeleteObject(b), DeleteObject(p);
}
static void circle(HDC dc, double cx, double cy, double r, COLORREF fill, COLORREF edge) {
  HBRUSH b = CreateSolidBrush(fill);
  HPEN p = CreatePen(PS_SOLID, 1, edge);
  HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, p);
  Ellipse(dc, X(cx - r), Y(cy - r), X(cx + r) + 1, Y(cy + r) + 1);
  SelectObject(dc, ob), SelectObject(dc, op);
  DeleteObject(b), DeleteObject(p);
}
/* text centred on (x, y) mm */
static void silk(HDC dc, HFONT f, COLORREF c, double x, double y, const char *s) {
  SelectObject(dc, f);
  SetTextColor(dc, c);
  SetTextAlign(dc, TA_CENTER | TA_BASELINE);
  TEXTMETRICA tm;
  GetTextMetricsA(dc, &tm);
  TextOutA(dc, X(x), Y(y) + tm.tmAscent / 2 - 1, s, (int)strlen(s));
  SetTextAlign(dc, TA_LEFT | TA_TOP);
}

static void text(HDC dc, HFONT f, COLORREF c, int x, int y, const char *s) {
  wchar_t w[300];
  MultiByteToWideChar(CP_UTF8, 0, s, -1, w, 300);
  SelectObject(dc, f);
  SetTextColor(dc, c);
  TextOutW(dc, x, y, w, (int)wcslen(w));
}

/* ---------------------------------------------------------------- parts */
static void chip0603(HDC dc, double x, double y, bool vertical, bool cap) {
  double l = 1.6 / 2, w = 0.8 / 2;
  double hx = vertical ? w : l, hy = vertical ? l : w;
  rect(dc, x - hx, y - hy, x + hx, y + hy, SILVER, SILVER);
  double bx = vertical ? w : l - 0.35, by = vertical ? l - 0.35 : w;
  rect(dc, x - bx, y - by, x + bx, y + by, cap ? RGB(170, 130, 80) : PART, cap ? RGB(150, 110, 70) : PART);
}

static void sot23(HDC dc, double x, double y, bool vertical) {
  double hx = vertical ? 0.65 : 1.45, hy = vertical ? 1.45 : 0.65;
  for (int i = -1; i <= 1; i += 2) {
    if (vertical) rect(dc, x - 1.1, y + i * 0.95 - 0.2, x - 0.65, y + i * 0.95 + 0.2, SILVER, SILVER);
    else rect(dc, x + i * 0.95 - 0.2, y + 0.65, x + i * 0.95 + 0.2, y + 1.1, SILVER, SILVER);
  }
  if (vertical) rect(dc, x + 0.65, y - 0.2, x + 1.1, y + 0.2, SILVER, SILVER);
  else rect(dc, x - 0.2, y - 1.1, x + 0.2, y - 0.65, SILVER, SILVER);
  rect(dc, x - hx, y - hy, x + hx, y + hy, PART, RGB(60, 60, 64));
}

static void tact_switch(HDC dc, double x, double y, bool down) {
  /* SMD tact switch: 4 legs, 5.2 mm body, round actuator */
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2) rect(dc, x + sx * 3.0 - 0.6, y + sy * 1.85 - 0.45, x + sx * 3.0 + 0.6, y + sy * 1.85 + 0.45, SILVER, STEEL);
  rect(dc, x - 2.6, y - 2.6, x + 2.6, y + 2.6, RGB(176, 178, 184), RGB(120, 122, 128));
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2) circle(dc, x + sx * 1.9, y + sy * 1.9, 0.35, RGB(120, 122, 128), RGB(120, 122, 128));
  if (down) {
    circle(dc, x, y, 1.9, RGB(110, 160, 255), RGB(110, 160, 255)); /* pressed: highlight ring */
    circle(dc, x, y, 1.55, RGB(18, 18, 20), RGB(0, 0, 0));
  } else {
    circle(dc, x, y, 1.75, RGB(36, 36, 40), RGB(10, 10, 12));
    circle(dc, x - 0.35, y - 0.35, 0.6, RGB(70, 70, 76), RGB(70, 70, 76));
  }
}

static void pico2w(HDC dc) {
  /* Raspberry Pi Pico 2 W (A1), rotated 90 deg: 51 x 21 mm, antenna flush with the right edge */
  double x0 = BW - 51.0, y0 = 5.0, x1 = BW, y1 = 26.0;
  for (int i = 0; i < 20; i++) { /* host-board pads under the castellations */
    double px = x0 + 1.37 + 2.54 * i;
    rect(dc, px - 0.8, y0 - 1.3, px + 0.8, y0 + 0.4, STEEL, STEEL);
    rect(dc, px - 0.8, y1 - 0.4, px + 0.8, y1 + 1.3, STEEL, STEEL);
  }
  rect(dc, x0, y0, x1, y1, RGB(28, 150, 76), RGB(20, 110, 56));
  for (int i = 0; i < 20; i++) {
    double px = x0 + 1.37 + 2.54 * i;
    rect(dc, px - 0.75, y0, px + 0.75, y0 + 2.0, GOLD, GOLD);
    rect(dc, px - 0.75, y1 - 2.0, px + 0.75, y1, GOLD, GOLD);
    circle(dc, px, y0 + 0.9, 0.45, RGB(240, 240, 236), GOLD);
    circle(dc, px, y1 - 0.9, 0.45, RGB(240, 240, 236), GOLD);
  }
  rrect(dc, 67.4, 9.5, 77.5, 21.4, 0.6, RGB(204, 206, 210), RGB(160, 162, 168), 1); /* RF shield (RP2350 + CYW43439) */
  rect(dc, 63.8, 18.6, 66.4, 21.7, RGB(204, 206, 210), RGB(160, 162, 168));
  for (int i = 0; i < 3; i++) circle(dc, 65.8, 9.9 + 2.5 * i, 0.55, RGB(240, 240, 236), GOLD); /* SWD pads */
  circle(dc, 83.7, 9.8, 1.25, GOLD, GOLD), circle(dc, 83.7, 9.8, 0.75, RGB(30, 34, 30), GOLD);
  circle(dc, 83.7, 21.3, 1.25, GOLD, GOLD), circle(dc, 83.7, 21.3, 0.75, RGB(30, 34, 30), GOLD);
  silk(dc, f_silk_s, SILK, BW - 1.3, 3.8, "A1");
}

static void display_module(HDC dc, bool powered) {
  /* 2.0" ST7789 module on J4: blue PCB (x 2..62, y 2..38), metal frame, glass */
  rect(dc, 2.0, 2.0, 62.0, 38.0, RGB(52, 90, 205), RGB(36, 66, 160));
  static const char *const PIN[8] = {"GND", "VCC", "SCL", "SDA", "RES", "DC", "CS", "BLK"};
  for (int i = 0; i < 8; i++) {
    double py = 6.0 + 2.54 * i;
    circle(dc, 4.5, py, 0.85, GOLD, GOLD);
    circle(dc, 4.5, py, 0.4, RGB(30, 30, 30), RGB(30, 30, 30));
    SelectObject(dc, f_silk_s);
    SetTextColor(dc, RGB(220, 228, 255));
    SetTextAlign(dc, TA_LEFT | TA_BASELINE);
    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    TextOutA(dc, X(5.8), Y(py) + tm.tmAscent / 2 - 1, PIN[i], (int)strlen(PIN[i]));
    SetTextAlign(dc, TA_LEFT | TA_TOP);
  }
  rect(dc, 10.5, 3.0, 60.0, 37.0, RGB(238, 238, 240), RGB(200, 200, 204)); /* panel frame */
  rect(dc, LCD_X0 - 0.7, LCD_Y0 - 0.7, LCD_X0 + LCD_WMM + 0.7, LCD_Y0 + LCD_HMM + 0.7, RGB(8, 8, 10), RGB(8, 8, 10));

  int x = X(LCD_X0), y = Y(LCD_Y0), w = X(LCD_X0 + LCD_WMM) - x, h = Y(LCD_Y0 + LCD_HMM) - y;
  if (!powered) {
    RECT r = {x, y, x + w, y + h};
    HBRUSH b = CreateSolidBrush(RGB(10, 10, 12));
    FillRect(dc, &r, b);
    DeleteObject(b);
    return;
  }
  BITMAPINFO bi;
  memset(&bi, 0, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = GLASS_W;
  bi.bmiHeader.biHeight = -GLASS_H;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  bool whole = w % GLASS_W == 0 && h % GLASS_H == 0;
  SetStretchBltMode(dc, whole ? COLORONCOLOR : HALFTONE);
  SetBrushOrgEx(dc, 0, 0, NULL);
  StretchDIBits(dc, x, y, w, h, 0, 0, GLASS_W, GLASS_H, pixels, &bi, DIB_RGB_COLORS, SRCCOPY);
}

static void board(HDC dc) {
  bool powered = emu_powered();
  uint8_t pressed = emu_buttons();

  /* outline: credit card, r = 3 mm */
  rrect(dc, 0.4, 0.8, BW + 0.4, BH + 0.8, 3.0, RGB(12, 13, 15), RGB(12, 13, 15), 1); /* shadow */
  rrect(dc, 0, 0, BW, BH, 3.0, MASK, MASK_EDGE, 2);

  /* mounting holes (H1/H4 sit under the display module) */
  const double H[4][2] = {{11.0, 3.2}, {3.2, BH - 3.2}, {BW - 3.0, BH - 3.0}, {31.5, 3.2}};
  for (int i = 0; i < 4; i++) circle(dc, H[i][0], H[i][1], 1.9, GOLD, GOLD), circle(dc, H[i][0], H[i][1], 1.1, DESK, DESK);
  silk(dc, f_silk, SILK, 3.2, BH - 6.4, "H2");
  silk(dc, f_silk, SILK, BW - 3.0, BH - 6.2, "H3");

  pico2w(dc);

  /* secure element corner + power switch for it (partly under the display) */
  rect(dc, 61.0, 30.0, 64.0, 33.0, PART, RGB(60, 60, 64));
  silk(dc, f_silk_s, SILK, 63.4, 28.8, "U2");
  sot23(dc, 69.5, 31.0, true);
  silk(dc, f_silk_s, SILK, 69.5, 28.2, "Q2");
  chip0603(dc, 72.0, 31.0, true, false);
  chip0603(dc, 62.5, 35.5, false, true);
  chip0603(dc, 66.0, 35.5, true, true);
  /* button RC filters */
  chip0603(dc, 75.0, 30.5, true, false), chip0603(dc, 76.6, 30.5, true, true);
  chip0603(dc, 78.2, 30.5, true, false), chip0603(dc, 79.8, 30.5, true, true);
  silk(dc, f_silk_s, SILK, 75.0, 28.4, "R26");
  silk(dc, f_silk_s, SILK, 79.6, 28.4, "C11");

  /* USB-C (J1) + ESD (U1), CC resistors, fuse */
  chip0603(dc, 36.0, 43.0, true, false), chip0603(dc, 44.0, 43.0, true, false);
  chip0603(dc, 36.0, 39.0, false, false), chip0603(dc, 40.0, 38.5, false, false);
  rect(dc, 38.6, 40.6, 41.4, 42.4, PART, RGB(60, 60, 64));
  for (int i = -1; i <= 1; i++) rect(dc, 40.0 + i * 0.95 - 0.2, 39.9, 40.0 + i * 0.95 + 0.2, 40.6, SILVER, SILVER);
  rect(dc, 45.2, 38.3, 46.8, 41.7, PART, RGB(60, 60, 64));
  rect(dc, 45.2, 38.3, 46.8, 38.9, SILVER, SILVER), rect(dc, 45.2, 41.1, 46.8, 41.7, SILVER, SILVER);
  silk(dc, f_silk_s, SILK, 34.6, 43.0, "R1"), silk(dc, f_silk_s, SILK, 45.5, 43.0, "R2");
  silk(dc, f_silk_s, SILK, 48.2, 40.0, "F1"), silk(dc, f_silk_s, SILK, 42.6, 41.5, "U1");
  for (int i = 0; i < 12; i++) rect(dc, 36.75 + i * 0.6 - 0.15, 45.3, 36.75 + i * 0.6 + 0.15, 46.4, GOLD, GOLD);
  rrect(dc, 35.6, 46.6, 44.4, BH, 0.8, SILVER, STEEL, 1);
  rect(dc, 36.4, 52.6, 43.6, BH - 0.1, RGB(40, 40, 44), RGB(40, 40, 44));
  rrect(dc, 34.9, 46.8, 36.1, 48.6, 0.5, GOLD, GOLD, 1), rrect(dc, 43.9, 46.8, 45.1, 48.6, 0.5, GOLD, GOLD, 1);
  rrect(dc, 34.9, 50.2, 36.1, 52.0, 0.5, GOLD, GOLD, 1), rrect(dc, 43.9, 50.2, 45.1, 52.0, 0.5, GOLD, GOLD, 1);
  silk(dc, f_silk_s, SILK, 40.0, 44.6, "J1");

  /* microSD (J3), card inserts from the bottom edge */
  rect(dc, 8.6, 37.6, 23.4, BH - 0.4, SILVER, STEEL);
  for (int i = 0; i < 4; i++) rect(dc, 9.6, 40.0 + 3.0 * i, 22.4, 40.4 + 3.0 * i, STEEL, STEEL);
  rect(dc, 7.8, 42.0, 8.6, 44.0, SILVER, STEEL), rect(dc, 23.4, 42.0, 24.2, 44.0, SILVER, STEEL);
  rect(dc, 7.8, 49.0, 8.6, 51.0, SILVER, STEEL), rect(dc, 23.4, 49.0, 24.2, 51.0, SILVER, STEEL);
  silk(dc, f_silk_s, SILK, 6.2, 46.0, "J3");

  /* SW1 power slide switch: ON = slider to the right */
  for (int i = -1; i <= 1; i++) {
    rect(dc, SW1_X + i * 2.5 - 0.5, SW1_Y - 2.9, SW1_X + i * 2.5 + 0.5, SW1_Y - 1.8, SILVER, STEEL);
    rect(dc, SW1_X + i * 2.5 - 0.5, SW1_Y + 1.8, SW1_X + i * 2.5 + 0.5, SW1_Y + 2.9, SILVER, STEEL);
  }
  rect(dc, SW1_X - 4.5, SW1_Y - 1.8, SW1_X + 4.5, SW1_Y + 1.8, SILVER, STEEL);
  rect(dc, SW1_X - 3.0, SW1_Y - 0.9, SW1_X + 3.0, SW1_Y + 0.9, RGB(40, 40, 44), RGB(40, 40, 44));
  double kx = powered ? SW1_X + 1.4 : SW1_X - 1.4;
  rect(dc, kx - 1.5, SW1_Y - 0.8, kx + 1.5, SW1_Y + 0.8, powered ? RGB(60, 190, 110) : RGB(120, 122, 128), RGB(20, 20, 20));
  silk(dc, f_silk, SILK, SW1_X, BH - 6.6, "PWR  ON>");

  /* debug: Tag-Connect J5 + test points (cover for production) */
  const double jx = 52.0, jy = 40.5;
  for (int i = 0; i < 3; i++) {
    circle(dc, jx - 1.27 + 1.27 * i, jy - 0.64, 0.4, GOLD, GOLD);
    circle(dc, jx - 1.27 + 1.27 * i, jy + 0.64, 0.4, GOLD, GOLD);
  }
  circle(dc, jx - 2.54, jy, 0.5, DESK, DESK), circle(dc, jx + 2.54, jy - 1.0, 0.5, DESK, DESK), circle(dc, jx + 2.54, jy + 1.0, 0.5, DESK, DESK);
  silk(dc, f_silk_s, SILK, jx, jy + 2.4, "J5");
  const double TP[8][2] = {{58.0, 40.5}, {60.5, 40.5}, {63.0, 40.5}, {65.5, 40.5}, {68.0, 40.5}, {70.5, 40.5}, {58.0, 43.0}, {60.5, 43.0}};
  for (int i = 0; i < 8; i++) {
    char l[8];
    circle(dc, TP[i][0], TP[i][1], 0.75, GOLD, RGB(170, 140, 50));
    snprintf(l, sizeof(l), "TP%d", i + 1);
    if (i < 6) silk(dc, f_silk_s, SILK, TP[i][0], TP[i][1] - 1.7, l);
    else silk(dc, f_silk_s, SILK, TP[i][0] + (i == 6 ? -2.6 : 2.6), TP[i][1], l);
  }
  silk(dc, f_silk, SILK, 66.0, 44.9, "DEBUG - cover for production");

  /* buttons */
  for (int i = 0; i < 4; i++) {
    bool down = powered && (pressed & BTN[i].bit);
    tact_switch(dc, BTN[i].x, BTN[i].y, down);
    silk(dc, f_silk, SILK, BTN[i].lx, BTN[i].ly, BTN[i].label);
  }

  display_module(dc, powered);
  /* BAT+ note along the left edge (vertical silk) */
  SelectObject(dc, f_silk_v);
  SetTextColor(dc, SILK);
  SetTextAlign(dc, TA_CENTER | TA_BASELINE);
  TextOutA(dc, X(1.0), Y(31.3), "BAT+ = pin 1", 12);
  SetTextAlign(dc, TA_LEFT | TA_TOP);

}

/* ---------------------------------------------------------------- whole window */
static void paint(HDC dc, RECT *client) {
  HBRUSH bg = CreateSolidBrush(DESK);
  FillRect(dc, client, bg);
  DeleteObject(bg);
  SetBkMode(dc, TRANSPARENT);
  board(dc);

  int x = panel_rc.left, y = panel_rc.top;
  text(dc, f_title, PANEL_TXT, x, y, "tredder emulator");
  y += 34;
  char l[256];
  snprintf(l, sizeof(l), "Firmware %s on the rev A board (emulated)", FW_VERSION);
  text(dc, f_text, DIM_TXT, x, y, l);
  y += 28;
  if (!emu_powered()) {
    text(dc, f_text, RGB(240, 180, 80), x, y, "Power OFF: click SW1 (PWR) to turn it on");
  } else {
    bool up = ws_connected();
    if (up) snprintf(l, sizeof(l), "Phone app:  connected (%s)", ws_peer());
    else snprintf(l, sizeof(l), "Phone app:  waiting on ws://127.0.0.1:%d", emu_opts.port);
    text(dc, f_text, up ? RGB(90, 210, 140) : PANEL_TXT, x, y, l);
  }
  y += 22;
  snprintf(l, sizeof(l), "OPTIGA Trust M (U2):  PIN tries left %u/10", se_model_tries_left());
  text(dc, f_text, PANEL_TXT, x, y, l);
  y += 22;
  snprintf(l, sizeof(l), "Flash + SE state:  %s", emu_opts.ephemeral ? "in memory (ephemeral)" : emu_opts.state_dir);
  text(dc, f_text, PANEL_TXT, x, y, l);
  y += 22;
  text(dc, f_text, RGB(240, 180, 80), x, y, "Development only: public test phrases + testnets.");
  y += 32;
  text(dc, f_text, DIM_TXT, x, y, "Click the buttons on the board, or use the keys:");
  y += 20;
  text(dc, f_text, DIM_TXT, x, y, "↑ ↓ = UP / DOWN,  Enter = CONFIRM,  Esc = BACK");
  y += 20;
  text(dc, f_text, DIM_TXT, x, y, "Typing a digit (PIN pad) or letter (word picker)");
  y += 20;
  text(dc, f_text, DIM_TXT, x, y, "makes the emulator press the buttons for you.");
  y += 20;
  text(dc, f_text, DIM_TXT, x, y, "SW1 (PWR) power-cycles: RAM lost, chips keep state.");
  y += 30;
  text(dc, f_title, PANEL_TXT, x, y, "Log");
  y += 32;
  char lines[LOG_LINES][160];
  int n = 0;
  emu_log_lines(lines, &n);
  int first = 0, room = (panel_rc.bottom - y) / 18;
  if (n > room) first = n - room;
  for (int i = first; i < n; i++, y += 18) text(dc, f_mono, DIM_TXT, x, y, lines[i]);
}

/* ---------------------------------------------------------------- input */
static int hit_button(int mx, int my) {
  for (int i = 0; i < 4; i++) {
    double dx = (mx - X(BTN[i].x)) / K, dy = (my - Y(BTN[i].y)) / K;
    if (dx * dx + dy * dy <= 4.0 * 4.0) return i;
  }
  return -1;
}
static bool hit_power(int mx, int my) {
  return mx >= X(SW1_X - 5.0) && mx <= X(SW1_X + 5.0) && my >= Y(SW1_Y - 3.0) && my <= Y(SW1_Y + 3.0);
}

static uint8_t key_bit(WPARAM vk) {
  switch (vk) {
    case VK_UP: return HAL_BTN_UP;
    case VK_DOWN: return HAL_BTN_DOWN;
    case VK_RETURN: case VK_SPACE: return HAL_BTN_CONFIRM;
    case VK_ESCAPE: case VK_BACK: return HAL_BTN_BACK;
    default: return 0;
  }
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM lp) {
  switch (m) {
    case WM_TIMER: {
      uint32_t f = lcd_model_snapshot(pixels);
      if (f != last_frame || InterlockedExchange(&repaint, 0)) {
        last_frame = f;
        InvalidateRect(h, NULL, FALSE);
      }
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      RECT rc;
      GetClientRect(h, &rc);
      HDC mem = CreateCompatibleDC(dc);
      HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
      HGDIOBJ old = SelectObject(mem, bmp);
      paint(mem, &rc);
      BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, old);
      DeleteObject(bmp);
      DeleteDC(mem);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(h, &p);
        SetCursor(LoadCursor(NULL, hit_button(p.x, p.y) >= 0 || hit_power(p.x, p.y) ? IDC_HAND : IDC_ARROW));
        return TRUE;
      }
      return DefWindowProcW(h, m, w, lp);
    case WM_LBUTTONDOWN: {
      int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
      if (hit_power(mx, my)) {
        emu_power_switch();
        InvalidateRect(h, NULL, FALSE);
        return 0;
      }
      int i = hit_button(mx, my);
      if (i >= 0 && emu_powered()) {
        emu_buttons_set(SRC_MOUSE, BTN[i].bit);
        SetCapture(h);
        InvalidateRect(h, NULL, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONUP:
      emu_buttons_set(SRC_MOUSE, 0);
      ReleaseCapture();
      InvalidateRect(h, NULL, FALSE);
      return 0;
    case WM_KEYDOWN:
    case WM_KEYUP: {
      uint8_t b = key_bit(w);
      if (b) {
        keys_mask = m == WM_KEYDOWN ? (uint8_t)(keys_mask | b) : (uint8_t)(keys_mask & ~b);
        emu_buttons_set(SRC_KEYS, keys_mask);
        InvalidateRect(h, NULL, FALSE);
      }
      return 0;
    }
    case WM_KILLFOCUS:
      keys_mask = 0;
      emu_buttons_set(SRC_KEYS, 0);
      emu_buttons_set(SRC_MOUSE, 0);
      return 0;
    case WM_CHAR:
      if (!emu_powered()) return 0;
      if ((w >= 'a' && w <= 'z') || (w >= '0' && w <= '9')) robot_type((char)w);
      else if (w >= 'A' && w <= 'Z') robot_type((char)(w - 'A' + 'a'));
      return 0;
    case WM_MOVE: { /* a power cycle restarts the process: reopen at the same place */
      RECT r;
      GetWindowRect(h, &r);
      char v[32];
      snprintf(v, sizeof(v), "%ld,%ld", r.left, r.top);
      SetEnvironmentVariableA("TREDDER_EMU_POS", v);
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(h, m, w, lp);
  }
}

static HFONT font(int px, int weight, const wchar_t *face, int angle) {
  return CreateFontW(-px, 0, angle, angle, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
}

static void layout(void) {
  const int margin = 24, panel_w = 410;
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  if (emu_opts.scale > 0) {
    K = emu_opts.scale * GLASS_W / LCD_WMM; /* --scale = size of one LCD pixel in screen pixels */
  } else {
    double kw = (wa.right - wa.left - panel_w - 3 * margin - 30) / BW;
    double kh = (wa.bottom - wa.top - 2 * margin - 60) / BH;
    K = kw < kh ? kw : kh;
    if (K > 16.0) K = 16.0;
    if (K < 7.0) K = 7.0;
  }
  OXp = margin, OYp = margin;
  int bw = (int)lround(BW * K), bh = (int)lround(BH * K);
  int ph = bh < 560 ? 560 : bh;
  panel_rc = (RECT){OXp + bw + margin + 8, OYp, OXp + bw + margin + 8 + panel_w, OYp + ph};
  win_w = panel_rc.right + margin;
  win_h = OYp + ph + margin;
}

void win_run(void) {
  layout();
  f_title = font(21, FW_BOLD, L"Segoe UI", 0);
  f_text = font(14, FW_NORMAL, L"Segoe UI", 0);
  f_mono = font(13, FW_NORMAL, L"Consolas", 0);
  int sp = (int)lround(1.0 * K);
  f_silk = font(sp < 9 ? 9 : sp, FW_BOLD, L"Consolas", 0);
  f_silk_s = font((int)lround(0.8 * K) < 8 ? 8 : (int)lround(0.8 * K), FW_SEMIBOLD, L"Consolas", 0);
  f_silk_v = font((int)lround(0.8 * K) < 8 ? 8 : (int)lround(0.8 * K), FW_BOLD, L"Consolas", 900);
  HINSTANCE inst = GetModuleHandleW(NULL);
  WNDCLASSW wc;
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = proc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hIcon = LoadIcon(inst, MAKEINTRESOURCE(1));
  wc.lpszClassName = L"tredder_emu";
  RegisterClassW(&wc);
  RECT r = {0, 0, win_w, win_h};
  DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  AdjustWindowRect(&r, style, FALSE);
  int px = CW_USEDEFAULT, py = CW_USEDEFAULT;
  char pos[32];
  if (GetEnvironmentVariableA("TREDDER_EMU_POS", pos, sizeof(pos)) && sscanf(pos, "%d,%d", &px, &py) != 2) px = py = CW_USEDEFAULT;
  hwnd = CreateWindowW(L"tredder_emu", L"tredder emulator - rev A board", style, px, py, r.right - r.left, r.bottom - r.top,
                       NULL, NULL, inst, NULL);
  ShowWindow(hwnd, SW_SHOW);
  SetTimer(hwnd, 1, 30, NULL);
  MSG msg;
  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  ExitProcess(0);
}
