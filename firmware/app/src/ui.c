/*
 * Screens + device flows. One screen is active at a time; each flow step
 * shows a screen and continues in its result callback. Four buttons:
 * Up / Down move, OK selects, Back goes back / rejects.
 *
 * Secrets handled here (mnemonic words being created or imported, PIN
 * digits) live in the static flow state F / S.pin and are wiped as soon as
 * a flow ends.
 */
#include "app/ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/firmware.h"
#include "app/gfx.h"
#include "app/hal.h"
#include "app/proto.h"
#include "app/rng.h"
#include "app/sdbackup.h"
#include "app/se.h"
#include "app/vault.h"
#include "rand.h"
#include "wallet/mnemonic.h"
#include "wallet/secure.h"

/* ---------------------------------------------------------------- look
   Same visual language as the phone app (which follows Electrum's mobile UI): Material Dark
   grey, a #424242 toolbar with a shadow line, light-blue accent, rounded "highlight panes",
   list rows with separators, and a button-legend bar at the bottom split by thin separators. */
#define C_BG RGB565(48, 48, 48)        /* #303030 background */
#define C_BAR RGB565(66, 66, 66)       /* #424242 toolbar */
#define C_PANE RGB565(62, 62, 62)      /* #3E3E3E highlight pane / keys */
#define C_DARK RGB565(36, 36, 36)      /* inset boxes, separators */
#define C_SHADOW RGB565(28, 28, 28)
#define C_FG RGB565(255, 255, 255)
#define C_DIM RGB565(158, 158, 158)
#define C_ACCENT RGB565(129, 212, 250) /* #81D4FA light blue */
#define C_ON_ACCENT RGB565(16, 16, 16)
#define C_SEL RGB565(74, 84, 92)       /* selected row: grey with a blue tint */
#define C_WARN RGB565(255, 238, 88)
#define C_DANGER RGB565(255, 128, 128)
#define C_OK RGB565(128, 255, 128)
#define C_CELL C_PANE
#define C_EDGE RGB565(96, 96, 96)      /* card outlines */

#define TITLE_H 30
#define BODY_Y 38
#define LINE_H 18
#define FOOT_Y 218
#define ROW_H 26

typedef enum { K_MENU, K_PAGES, K_PIN, K_WORD } kind_t;
typedef void (*result_fn)(int r);

#define MAX_ITEMS 32
#define ITEM_LEN 24

static struct {
  kind_t kind;
  char title[28];
  uint8_t tone;
  char lines[12][32];
  uint8_t nlines;
  char items[MAX_ITEMS][ITEM_LEN];
  uint8_t nitems, cursor, cols, scroll;
  bool wide_first; /* first item spans the grid (word picker "✓ word") */
  const wl_review_t *rv;
  /* review pages cut into screen-sized views, so no line is ever off screen */
  uint8_t vpage[2 * WL_REVIEW_MAX_PAGES], vfirst[2 * WL_REVIEW_MAX_PAGES], vcount[2 * WL_REVIEW_MAX_PAGES];
  uint8_t nviews;
  uint8_t page; /* current view */
  bool final_ok; /* pages: OK on the last page means "approve" (vs. just "continue") */
  char hint[40];
  result_fn on;
  /* PIN pad */
  char pin[VAULT_PIN_MAX + 1];
  uint8_t pinlen;
  /* word picker */
  char prefix[9];
  int picked;
  bool dirty;
  uint8_t layout; /* L_*: welcome / locked / home drawn as screens, else a list */
} S;

static struct {
  bool app_connected, paired;
  int8_t battery; /* -1 unknown */
  bool charging;
  uint32_t lockout_until;
  uint32_t now;
  char balance[20]; /* total value as the phone app reported it (display only, not verified) */
  uint16_t batt_mv;
  uint8_t batt_state; /* hal_vbat_state_t */
  uint16_t batt_raw_mv;
  uint8_t batt_mode; /* top-right battery: BATT_BAR / BATT_PCT / BATT_BOTH (RAM: the demo saves nothing) */
} D;
enum { BATT_BAR = 0, BATT_PCT, BATT_BOTH };

/* ---------------------------------------------------------------- screen setup */
static void screen(kind_t k, const char *title, wl_tone_t tone, result_fn on) {
  wl_wipe(S.pin, sizeof(S.pin));
  memset(&S, 0, sizeof(S));
  S.kind = k;
  snprintf(S.title, sizeof(S.title), "%s", title);
  S.tone = (uint8_t)tone;
  S.on = on;
  S.cols = 1;
  S.dirty = true;
}
static void line(const char *s) {
  if (S.nlines < 12) snprintf(S.lines[S.nlines++], sizeof(S.lines[0]), "%s", s);
}
static void item(const char *s) {
  if (S.nitems < MAX_ITEMS) snprintf(S.items[S.nitems++], ITEM_LEN, "%s", s);
}
static void hint(const char *s) { snprintf(S.hint, sizeof(S.hint), "%s", s); }

static void shuffle_u8(uint8_t *a, size_t n) {
  for (size_t i = n - 1; i > 0; i--) {
    uint32_t r = random32() % (uint32_t)(i + 1);
    uint8_t t = a[i];
    a[i] = a[r];
    a[r] = t;
  }
}

static int line_h(const char *l);
static wl_review_t local_rv; /* pages built by flows (not protocol prompts) */

/* ---------------------------------------------------------------- drawing */
/* Icons: Ionicons glyphs U+E000 + index (tools/gen_font.py ICONS, same order). */
enum {
  IC_LOCK, IC_UNLOCK, IC_ADD, IC_DOWNLOAD, IC_SAVE, IC_SHIELD, IC_TRASH, IC_KEY, IC_BLUETOOTH, IC_EYE_OFF,
  IC_WALLET, IC_PHONE, IC_CHECK, IC_WARNING, IC_CLOSE, IC_INFO, IC_HOURGLASS, IC_REFRESH, IC_UNDO, IC_SWAP,
  IC_DOC, IC_ALERT, IC_BATTERY, IC_SETTINGS, IC_LOGO, IC_NONE = 255
};

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

static void icon(int x, int y, uint8_t ic, const wl_font_t *f, uint16_t color) {
  if (ic == IC_NONE) return;
  unsigned cp = 0xE000u + ic;
  char s[4] = {(char)(0xE0 | (cp >> 12)), (char)(0x80 | ((cp >> 6) & 0x3F)), (char)(0x80 | (cp & 0x3F)), 0};
  gfx_text(x, y, s, f, color);
}

/* Filled circle (rounded square with radius = half the size). */
static void disc(int cx, int cy, int r, uint16_t color) { gfx_round_rect(cx - r, cy - r, 2 * r, 2 * r, r, color); }

static uint16_t tone_color(uint8_t t) { return t == WL_TONE_DANGER ? C_DANGER : t == WL_TONE_WARN ? C_WARN : C_FG; }

/* Icon for a menu entry, by its text (the texts are the firmware's own, see the flows below). */
static uint8_t item_icon(const char *s) {
  static const struct {
    const char *prefix;
    uint8_t ic;
  } MAP[] = {
      {"Create new wallet", IC_ADD}, {"Import seed", IC_DOWNLOAD}, {"Restore microSD", IC_SAVE}, {"Unlock", IC_UNLOCK},
      {"Lock", IC_LOCK},           {"Blind signing", IC_EYE_OFF}, {"Back up", IC_SAVE},          {"Wipe", IC_TRASH},
      {"Cancel", IC_UNDO},         {"Enter it again", IC_REFRESH}, {"Import & sweep", IC_SWAP},  {"Import as-is", IC_DOWNLOAD},
      {"tredder-", IC_DOC},        {"✓ ", IC_CHECK},           {"New wallet", IC_ADD},       {"Import wallet", IC_DOWNLOAD},
      {"Restore from", IC_SAVE},   {"Backup", IC_SAVE},           {"Unpair", IC_BLUETOOTH},     {"Settings", IC_SETTINGS},
      {"Battery", IC_BATTERY},     {"Show: ", IC_BATTERY},        {"Log", IC_DOC},
      {"About", IC_INFO},
  };
  for (unsigned i = 0; i < sizeof(MAP) / sizeof(MAP[0]); i++)
    if (!strncmp(s, MAP[i].prefix, strlen(MAP[i].prefix))) return MAP[i].ic;
  if (strstr(s, " words")) return IC_KEY;
  return IC_NONE;
}

/* "12. word" style lines: the number in accent, the rest in white. */
static int numbered_prefix(const char *l) {
  int i = 0;
  while (l[i] == ' ') i++;
  int d = i;
  while (l[i] >= '0' && l[i] <= '9') i++;
  return i > d && l[i] == '.' && l[i + 1] == ' ' ? i + 1 : 0;
}

static void chevron(int cx, int cy, uint16_t c) { /* the fonts have no U+203A: drawn */
  for (int d = 0; d < 5; d++) {
    gfx_fill(cx + d, cy - 4 + d, 2, 1, c);
    gfx_fill(cx + d, cy + 4 - d, 2, 1, c);
  }
}

/* List rows: icon, text, chevron on the selected one (a rounded "pill" with an accent edge). */
static void draw_rows(int y, int row_h) {
  int rows = (FOOT_Y - 2 - y) / row_h;
  if (rows < 1) rows = 1;
  if (S.cursor < S.scroll) S.scroll = S.cursor;
  if (S.cursor >= S.scroll + rows) S.scroll = (uint8_t)(S.cursor - rows + 1);
  for (int i = S.scroll; i < S.nitems && i < S.scroll + rows; i++) {
    int ry = y + (i - S.scroll) * row_h;
    bool sel = i == S.cursor;
    const char *txt = S.items[i];
    uint8_t ic = item_icon(txt);
    bool danger = !strncmp(txt, "Wipe", 4);
    if (!strncmp(txt, "✓ ", 4)) txt += 4; /* the icon says it */
    /* a card per entry: outlined, filled + accent outline when selected */
    uint16_t edge = sel ? (danger ? C_DANGER : C_ACCENT) : C_EDGE;
    gfx_round_rect(6, ry + 2, GFX_W - 12, row_h - 4, 9, edge);
    gfx_round_rect(sel ? 8 : 7, ry + (sel ? 4 : 3), GFX_W - (sel ? 16 : 14), row_h - (sel ? 8 : 6), 8, sel ? C_SEL : C_BG);
    uint16_t fg = danger ? C_DANGER : sel ? C_ACCENT : C_FG;
    int tx = 18;
    if (ic != IC_NONE) {
      icon(18, ry + (row_h - wl_icons.height) / 2, ic, &wl_icons, danger ? C_DANGER : sel ? C_ACCENT : C_DIM);
      tx = 44;
    }
    gfx_text(tx, ry + (row_h - wl_font_bold.height) / 2, txt, &wl_font_bold, fg);
    chevron(GFX_W - 24, ry + row_h / 2, sel ? (danger ? C_DANGER : C_ACCENT) : C_DIM);
  }
  if (S.scroll > 0) gfx_text(GFX_W - 18, y - 2, "▲", &wl_font_body, C_DIM);
  if (S.scroll + rows < S.nitems) gfx_text(GFX_W - 18, FOOT_Y - 17, "▼", &wl_font_body, C_DIM);
}

static void draw_items(int y) {
  if (!S.nitems) return;
  if (S.cols == 1) {
    draw_rows(y, ROW_H);
    return;
  }
  /* grid: PIN pad (digits, ←, ✓) and the word picker's letters */
  int first = 0;
  if (S.wide_first) {
    bool sel = S.cursor == 0;
    gfx_round_rect(6, y, GFX_W - 12, 28, 8, sel ? C_OK : C_PANE);
    icon(14, y + 5, IC_CHECK, &wl_icons, sel ? C_ON_ACCENT : C_OK);
    gfx_text(38, y + (28 - wl_font_bold.height) / 2, S.items[0] + 4, &wl_font_bold, sel ? C_ON_ACCENT : C_OK);
    y += 32;
    first = 1;
  }
  bool pin = S.kind == K_PIN;
  /* the PIN pad: two rows, 1 2 3 4 5 6 / 7 8 9 0 ← ✓ (the user preferred it to a phone-style pad) */
  int cw = (GFX_W - 12) / S.cols, ch = pin ? 44 : 28;
  const wl_font_t *f = pin ? &wl_font_title : &wl_font_bold;
  for (int i = first; i < S.nitems; i++) {
    int k = i - first, cx = 6 + (k % S.cols) * cw, cy = y + (k / S.cols) * ch;
    bool sel = i == S.cursor;
    bool enter = pin && !strcmp(S.items[i], "✓"), del = pin && !strcmp(S.items[i], "←");
    uint16_t bg = sel ? (enter ? C_OK : del ? C_DANGER : C_ACCENT) : C_PANE;
    uint16_t fg = sel ? C_ON_ACCENT : enter ? C_OK : del ? C_DANGER : C_FG;
    gfx_round_rect(cx + 2, cy + 2, cw - 4, ch - 4, pin ? 10 : 6, bg);
    if (pin && !sel) gfx_fill(cx + 8, cy + ch - 4, cw - 16, 1, C_DARK); /* key "depth" */
    int tw = gfx_text_width(S.items[i], f);
    gfx_text(cx + (cw - tw) / 2, cy + (ch - f->height) / 2, S.items[i], f, fg);
  }
}

/* Level to draw: while charging the fill climbs from the real level to full in steps (phone-style). */
static int shown_level(void) {
  int lvl = D.battery > 100 ? 100 : D.battery;
  if (D.charging && lvl < 100) lvl += (100 - lvl) * (int)((D.now / 450u) % 5u) / 4;
  return lvl;
}

/* A battery of any size: outline, nub, fill, a bolt while charging. */
static void battery_shape(int x, int y, int w, int h, int lvl, uint16_t fill, uint16_t bg) {
  int r = h / 4 > 2 ? h / 4 : 2;
  gfx_round_rect(x, y, w, h, r, C_DIM);
  gfx_round_rect(x + 1, y + 1, w - 2, h - 2, r - 1, bg);
  gfx_fill(x + w, y + h / 4, h / 6 > 2 ? h / 6 : 2, h - h / 2, C_DIM);
  int fw = (w - 4) * lvl / 100;
  if (fw < 1 && lvl > 0) fw = 1;
  gfx_fill(x + 2, y + 2, fw, h - 4, fill);
  if (D.charging) { /* bolt: two slanted bars, in the background colour so it shows on the fill */
    int cx = x + w / 2, cy = y + h / 2, s = h / 3 > 2 ? h / 3 : 2;
    for (int i = 0; i < s; i++) {
      gfx_fill(cx - i / 2, cy - s + i, 2 + s / 3, 1, bg);
      gfx_fill(cx - s / 3 + i / 2 - 1, cy + i, 2 + s / 3, 1, bg);
    }
    gfx_fill(cx - s / 2, cy - 1, s + 1, 2, bg);
  }
}

/* Top right: bar, percent or both (Settings > Battery). Green while charging, red at <= 15 %. */
static void draw_battery(int right, int y) {
  if (D.battery < 0) return;
  const int w = 22, h = 12;
  uint16_t c = D.charging ? C_OK : D.battery <= 15 ? C_DANGER : C_FG;
  int x = right - w - 2;
  if (D.batt_mode != BATT_PCT) battery_shape(x, y, w, h, shown_level(), c, C_BAR);
  if (D.batt_mode != BATT_BAR) {
    char t[8];
    snprintf(t, sizeof(t), "%d%%", D.battery);
    int tw = gfx_text_width(t, &wl_font_body);
    gfx_text((D.batt_mode == BATT_BOTH ? x - 4 : right) - tw, y - 3, t, &wl_font_body, c);
  }
}

/* Phone link: Bluetooth mark, green = paired app connected, yellow = unpaired app, grey = none. */
static void draw_link(int x, int y) {
  icon(x, y, IC_BLUETOOTH, &wl_icons, D.app_connected ? (D.paired ? C_OK : C_WARN) : C_DIM);
}

/* Bottom bar: "key caps" for the buttons (▲▼ ✓ ✗) with their meaning. */
static void draw_legend(void) {
  if (!S.hint[0]) return;
  gfx_fill(0, FOOT_Y, GFX_W, GFX_H - FOOT_Y, C_BAR);
  gfx_fill(0, FOOT_Y, GFX_W, 1, C_SHADOW);
  char seg[3][24];
  int n = 0;
  const char *p = S.hint;
  while (*p && n < 3) {
    const char *e = strstr(p, "  ");
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (len >= sizeof(seg[0])) len = sizeof(seg[0]) - 1;
    memcpy(seg[n], p, len);
    seg[n++][len] = 0;
    if (!e) break;
    p = e;
    while (*p == ' ') p++;
  }
  int sw = GFX_W / n, fy = FOOT_Y + (GFX_H - FOOT_Y - wl_font_body.height) / 2 + 1;
  for (int i = 0; i < n; i++) {
    char *label = strchr(seg[i], ' ');
    const char *key = seg[i];
    if (label) *label++ = 0;
    bool symbol = (unsigned char)key[0] >= 0xE2; /* ▲ ▼ ✓ ✗: a key cap */
    int kw = symbol ? gfx_text_width(key, &wl_font_bold) + 10 : 0;
    int lw = gfx_text_width(label && symbol ? label : seg[i], &wl_font_body);
    int x = i * sw + (sw - kw - (symbol ? 6 : 0) - lw) / 2;
    if (symbol) {
      gfx_round_rect(x, FOOT_Y + 3, kw, GFX_H - FOOT_Y - 6, 4, C_DARK);
      uint16_t kc = !strcmp(key, "✓") ? C_OK : !strcmp(key, "✗") ? C_DANGER : C_ACCENT;
      gfx_text(x + 5, fy, key, &wl_font_bold, kc);
      x += kw + 6;
    }
    gfx_text(x, fy, label && symbol ? label : seg[i], &wl_font_body, C_FG);
  }
}

static void toolbar(const char *title, uint8_t tone, const char *suffix, bool logo) {
  gfx_fill(0, 0, GFX_W, TITLE_H, C_BAR);
  gfx_fill(0, TITLE_H, GFX_W, 2, C_SHADOW);
  int tx = 10;
  if (logo) {
    gfx_round_rect(6, 3, 24, 24, 5, C_DARK);
    icon(9, 6, IC_LOGO, &wl_icons, C_FG);
    tx = 38;
  }
  if (tone != WL_TONE_NORMAL) {
    gfx_fill(0, 0, 4, TITLE_H, tone_color(tone));
    icon(10, (TITLE_H - wl_icons.height) / 2, tone == WL_TONE_DANGER ? IC_ALERT : IC_WARNING, &wl_icons, tone_color(tone));
    tx = 34;
  }
  tx = gfx_text(tx, (TITLE_H - wl_font_title.height) / 2 + 1, title, &wl_font_title, tone_color(tone));
  if (suffix) gfx_text(tx + 8, (TITLE_H - wl_font_body.height) / 2 + 1, suffix, &wl_font_body, C_ACCENT);
  draw_battery(GFX_W - 28, 9);
  draw_link(GFX_W - 24, 6);
}

/* Page dots under the title bar (multi-page reviews, word lists). */
static void page_dots(void) {
  if (S.nviews < 2 || S.nviews > 12) return;
  int gap = 12, x = GFX_W / 2 - (S.nviews - 1) * gap / 2;
  int dy = S.final_ok ? FOOT_Y - 4 : FOOT_Y - 7;
  for (int i = 0; i < S.nviews; i++) disc(x + i * gap, dy, i == S.page ? 4 : 3, i == S.page ? C_ACCENT : C_DARK);
}

/* ---- special layouts (same screen data, drawn as a proper screen instead of a list) */
enum { L_LIST = 0, L_WELCOME, L_LOCKED, L_HOME, L_BATTERY, L_LOG };

/* Settings > Log: the device log on its own screen (no PC needed), refreshed every 2 s; the top line
   is the live battery reading. The log never holds secrets (hal.h). */
static void draw_log(void) {
  static char buf[1600];
  char top[48];
  if (D.battery >= 0) snprintf(top, sizeof(top), "Battery %d%%, %u mV%s", D.battery, D.batt_mv, D.charging ? ", charging" : "");
  else if (D.batt_state == 1) snprintf(top, sizeof(top), "Battery: pin 34 not connected");
  else if (D.batt_state == 2) snprintf(top, sizeof(top), "Battery: pin 34 too high (%u mV)", D.batt_raw_mv);
  else if (D.batt_state == 3) snprintf(top, sizeof(top), "Battery: below 2.5 V (%u mV)", D.batt_raw_mv);
  else snprintf(top, sizeof(top), "Battery: no reading yet");
  gfx_round_rect(4, BODY_Y - 4, GFX_W - 8, 22, 6, C_PANE);
  gfx_text(10, BODY_Y - 2, top, &wl_font_body, D.battery >= 0 ? C_OK : C_WARN);

  size_t n = hal_log_recent(buf, sizeof(buf));
  /* wrap into rows of 34 characters (the body font is 9 px wide), keep the newest rows */
  enum { COLS = 34, ROWS = 9 };
  static char rows[40][COLS + 1];
  int nr = 0;
  for (size_t i = 0; i < n;) {
    size_t e = i;
    while (e < n && buf[e] != '\n') e++;
    const char *l = buf + i;
    size_t len = e - i;
    if (len > 3 && l[0] == '[') { /* "[  12345 ms] text" -> "12s text" */
      const char *rb = memchr(l, ']', len);
      if (rb) {
        unsigned long ms = strtoul(l + 1, NULL, 10);
        char pre[12];
        int k = snprintf(pre, sizeof(pre), "%lus ", ms / 1000ul);
        size_t skip = (size_t)(rb + 2 - l) < len ? (size_t)(rb + 2 - l) : len;
        static char line[200];
        size_t body = len - skip > sizeof(line) - 13 ? sizeof(line) - 13 : len - skip;
        memcpy(line, pre, (size_t)k);
        memcpy(line + k, l + skip, body);
        l = line, len = (size_t)k + body;
      }
    }
    for (size_t o = 0; o < len || o == 0; o += COLS) {
      if (nr == 40) memmove(rows, rows + 1, sizeof(rows[0]) * 39), nr = 39;
      size_t c = len - o > COLS ? COLS : len - o;
      memcpy(rows[nr], l + o, c);
      rows[nr][c] = 0;
      nr++;
      if (len == 0) break;
    }
    i = e + 1;
  }
  int first = nr > ROWS ? nr - ROWS : 0;
  for (int r = first, y = BODY_Y + 22; r < nr; r++, y += LINE_H) {
    bool warn = strstr(rows[r], "impossible") || strstr(rows[r], "fail") || strstr(rows[r], "FAULT") || strstr(rows[r], "low");
    gfx_text(6, y, rows[r], &wl_font_body, warn ? C_WARN : C_FG);
  }
}

/* Settings > Battery: a big bar with the percentage, voltage and state, then the display choice. */
static void draw_battery_screen(void) {
  int x = 40, y = BODY_Y + 4, w = 228, h = 50;
  int lvl = D.battery < 0 ? 0 : shown_level();
  uint16_t c = D.battery < 0 ? C_DIM : D.charging ? C_OK : D.battery <= 15 ? C_DANGER : C_ACCENT;
  battery_shape(x, y, w, h, lvl, c, C_DARK);
  char t[32];
  if (D.battery < 0) snprintf(t, sizeof(t), "-");
  else snprintf(t, sizeof(t), "%d%%", D.battery);
  int tw = gfx_text_width(t, &wl_font_big);
  /* the number on a dark plate so it reads on any fill */
  gfx_round_rect(x + (w - tw) / 2 - 8, y + (h - wl_font_big.height) / 2 - 1, tw + 16, wl_font_big.height + 2, 6, C_DARK);
  gfx_text(x + (w - tw) / 2, y + (h - wl_font_big.height) / 2, t, &wl_font_big, C_FG);
  if (D.battery < 0) {
    /* no reading: say why, so the wiring can be fixed without a PC (hal_vbat_state_t) */
    const char *a = "No battery connected", *b = NULL;
    char raw[32];
    if (D.batt_state == 1) a = "No battery wiring", b = "Pin 34 is not connected";
    else if (D.batt_state == 2) {
      snprintf(raw, sizeof(raw), "Pin 34 reads %u.%02u V: too high", D.batt_raw_mv / 1000u, (D.batt_raw_mv % 1000u) / 10u);
      a = raw, b = "R11 or its GND is missing";
    } else if (D.batt_state == 3) a = "Battery below 2.5 V", b = "or R10 not connected";
    tw = gfx_text_width(a, &wl_font_body);
    gfx_text((GFX_W - tw) / 2, y + h + 4, a, &wl_font_body, D.batt_state >= 2 ? C_WARN : C_DIM);
    if (b) {
      tw = gfx_text_width(b, &wl_font_body);
      gfx_text((GFX_W - tw) / 2, y + h + 4 + LINE_H, b, &wl_font_body, C_WARN);
    }
    draw_rows(y + h + (b ? 42 : 30), b ? 28 : 30);
    return;
  }
  snprintf(t, sizeof(t), "%s  •  %u.%02u V", D.charging ? "Charging" : "On battery", D.batt_mv / 1000u, (D.batt_mv % 1000u) / 10u);
  tw = gfx_text_width(t, &wl_font_body);
  gfx_text((GFX_W - tw) / 2, y + h + 6, t, &wl_font_body, D.charging ? C_OK : C_DIM);
  draw_rows(y + h + 30, 30);
}

static void draw_welcome(void) {
  int y = BODY_Y;
  for (uint8_t i = 0; i < S.nlines; i++, y += LINE_H) { /* demo builds: the warning */
    if (i == 0) icon(10, y, IC_WARNING, &wl_icons, C_WARN);
    gfx_text(32, y, S.lines[i], &wl_font_body, C_WARN);
  }
  int y0 = S.nlines ? y + 4 : BODY_Y + 4, rh = (FOOT_Y - 2 - y0) / (S.nitems ? S.nitems : 1);
  draw_rows(y0, rh > 52 ? 52 : rh); /* every card fits, however many there are */
}

static void draw_locked(void) {
  disc(GFX_W / 2, 82, 38, C_PANE);
  icon(GFX_W / 2 - 24, 58, IC_LOCK, &wl_icons_big, C_ACCENT);
  const char *t = "Locked";
  gfx_text((GFX_W - gfx_text_width(t, &wl_font_title)) / 2, 126, t, &wl_font_title, C_FG);
  int y = 152;
  for (uint8_t i = 1; i < S.nlines && y < 186; i++, y += LINE_H) { /* line 0 is "Locked" */
    uint16_t c = strstr(S.lines[i], "wrong") ? C_DANGER : C_WARN;
    gfx_text((GFX_W - gfx_text_width(S.lines[i], &wl_font_body)) / 2, y, S.lines[i], &wl_font_body, c);
  }
  /* the one action as a big button */
  int bw = 180, bx = (GFX_W - bw) / 2, by = 188;
  gfx_round_rect(bx, by, bw, 26, 13, C_ACCENT);
  int w = gfx_text_width("Unlock", &wl_font_bold) + 26;
  icon(bx + (bw - w) / 2, by + 4, IC_UNLOCK, &wl_icons, C_ON_ACCENT);
  gfx_text(bx + (bw - w) / 2 + 26, by + (26 - wl_font_bold.height) / 2, "Unlock", &wl_font_bold, C_ON_ACCENT);
}

static void draw_home(void) {
  /* the money, as the phone app reported it (the device has no internet; display only) */
  int y = BODY_Y - 2;
  if (D.balance[0]) {
    gfx_text(14, y, D.balance, &wl_font_big, C_FG);
    gfx_text(14, y + wl_font_big.height, "total, from your phone", &wl_font_body, C_DIM);
  } else {
    gfx_text(14, y, "$ \xe2\x80\x94", &wl_font_big, C_DIM);
    gfx_text(14, y + wl_font_big.height, D.app_connected ? "waiting for the app" : "open the app to see it", &wl_font_body, C_DIM);
  }
  y += wl_font_big.height + LINE_H + 2;
  for (uint8_t i = 2; i < S.nlines && i < 3; i++, y += LINE_H) { /* e.g. "Imported seed waiting" */
    icon(12, y, IC_WARNING, &wl_icons, C_WARN);
    gfx_text(34, y, S.lines[i], &wl_font_body, C_WARN);
  }
  draw_rows(y, (FOOT_Y - 2 - y) / 4 > 34 ? 34 : (FOOT_Y - 2 - y) / 4);
}

/* Approval screens (the last page of a request, "Pair phone?" too): ✗ and ✓ as two buttons. */
static bool approve_view(void) { return S.kind == K_PAGES && S.final_ok && S.page == S.nviews - 1; }

static void draw_decision(void) {
  const int y = FOOT_Y - 12, h = GFX_H - y - 3, w = 96;
  gfx_round_rect(6, y, w, h, 8, C_DANGER);
  gfx_round_rect(8, y + 2, w - 4, h - 4, 7, C_BG);
  int tw = gfx_text_width("✗ reject", &wl_font_bold);
  gfx_text(6 + (w - tw) / 2, y + (h - wl_font_bold.height) / 2, "✗ reject", &wl_font_bold, C_DANGER);
  gfx_round_rect(GFX_W - 6 - w, y, w, h, 8, C_OK);
  tw = gfx_text_width("✓ accept", &wl_font_bold);
  gfx_text(GFX_W - 6 - w + (w - tw) / 2, y + (h - wl_font_bold.height) / 2, "✓ accept", &wl_font_bold, C_ON_ACCENT);
}

/* Big icon on single result / info screens. */
static uint8_t info_icon(const wl_page_t *pg) {
  if (pg->tone == WL_TONE_DANGER) return IC_CLOSE;
  if (pg->tone == WL_TONE_WARN) return IC_WARNING;
  if (!strcmp(S.hint, "please wait...")) return IC_HOURGLASS;
  static const char *const OK[] = {"saved", "Ready", "Restored", "Wiped", "OK", "Imported", "paired", "sweep"};
  for (unsigned i = 0; i < sizeof(OK) / sizeof(OK[0]); i++)
    if (strstr(pg->title, OK[i])) return IC_CHECK;
  if (strstr(pg->title, "microSD") || strstr(pg->title, "Backup") || strstr(pg->title, "backup")) return IC_SAVE;
  return IC_INFO;
}

static uint16_t info_icon_color(uint8_t ic) {
  return ic == IC_CLOSE ? C_DANGER : ic == IC_WARNING ? C_WARN : ic == IC_CHECK ? C_OK : C_ACCENT;
}

static void draw(void) {
  gfx_fill(0, 0, GFX_W, GFX_H, C_BG);
  uint8_t tone = S.tone;
  const wl_page_t *pg = NULL;
  if (S.kind == K_PAGES) {
    pg = &S.rv->pages[S.vpage[S.page]];
    tone = pg->tone;
  }
  toolbar(pg ? pg->title : S.title, tone, NULL, S.kind == K_MENU && (S.layout == L_WELCOME || S.layout == L_HOME));

  if (S.kind == K_MENU && S.layout == L_WELCOME) draw_welcome();
  else if (S.kind == K_MENU && S.layout == L_LOCKED) draw_locked();
  else if (S.kind == K_MENU && S.layout == L_HOME) draw_home();
  else if (S.kind == K_MENU && S.layout == L_BATTERY) draw_battery_screen();
  else if (S.kind == K_MENU && S.layout == L_LOG) draw_log();
  else {
    int y = S.kind == K_PIN ? BODY_Y - 2 : BODY_Y;
    uint8_t first = pg ? S.vfirst[S.page] : 0, n = pg ? S.vcount[S.page] : S.nlines;
    bool words = pg && n > 0;
    for (uint8_t i = 0; words && i < n; i++) words = numbered_prefix(pg->lines[first + i]) > 0;
    /* one-screen info / result pages from the device's own flows get a big icon */
    bool hero = pg && !words && S.nviews == 1 && S.rv == &local_rv && n <= 7;
    if (words) { /* seed words / backup password: one card per word, large */
      for (uint8_t i = 0; i < n; i++, y += 42) {
        const char *l = pg->lines[first + i];
        int k = numbered_prefix(l);
        char num[6];
        snprintf(num, sizeof(num), "%.*s", k - 1, l);
        const char *num_s = num;
        while (*num_s == ' ') num_s++;
        gfx_round_rect(8, y, GFX_W - 16, 38, 8, C_PANE);
        disc(30, y + 19, 13, C_DARK);
        gfx_text(30 - gfx_text_width(num_s, &wl_font_bold) / 2, y + 19 - wl_font_bold.height / 2, num_s, &wl_font_bold, C_ACCENT);
        gfx_text(56, y + (38 - wl_font_title.height) / 2 + 1, l + k + 1, &wl_font_title, C_FG);
      }
    } else {
      if (hero) {
        uint8_t ic = info_icon(pg);
        disc(GFX_W / 2, BODY_Y + 24, 26, C_PANE);
        icon(GFX_W / 2 - 24, BODY_Y, ic, &wl_icons_big, info_icon_color(ic));
        y = BODY_Y + 58;
      } else if (pg && n) { /* review pages sit on a highlight pane */
        int h = 0;
        for (uint8_t i = 0; i < n; i++) h += line_h(pg->lines[first + i]);
        gfx_round_rect(4, BODY_Y - 4, GFX_W - 8, h + 8 > FOOT_Y - BODY_Y - 14 ? FOOT_Y - BODY_Y - 14 : h + 8, 8, C_PANE);
      }
      for (uint8_t i = 0; i < n; i++) {
        const char *l = pg ? pg->lines[first + i] : S.lines[i];
        if (l[0] == '\x01' && S.kind == K_PIN) { /* PIN: wide entry box, one slot per digit */
          gfx_round_rect(40, y, 240, 32, 9, C_EDGE);
          gfx_round_rect(41, y + 1, 238, 30, 8, C_DARK);
          int slots = S.pinlen > 4 ? S.pinlen : 4;
          if (S.pinlen < VAULT_PIN_MAX && S.pinlen >= 4) slots++;
          int sx = GFX_W / 2 - (slots - 1) * 12;
          for (int d = 0; d < slots; d++) {
            disc(sx + d * 24, y + 16, 7, d < S.pinlen ? C_ACCENT : C_EDGE);
            if (d >= S.pinlen) disc(sx + d * 24, y + 16, 5, C_DARK);
          }
          y += 36;
        } else if (l[0] == '\x01') { /* big line (codes, typed prefix): inset box, accent */
          int w = gfx_text_width(l + 1, &wl_font_big), bw = w + 32 < 160 ? 160 : w + 32;
          gfx_round_rect((GFX_W - bw) / 2, y - 1, bw, wl_font_big.height + 2, 8, C_DARK);
          gfx_text((GFX_W - w) / 2, y, l + 1, &wl_font_big, C_ACCENT);
          y += wl_font_big.height + 2;
        } else {
          int k = numbered_prefix(l);
          int x = hero ? (GFX_W - gfx_text_width(l, &wl_font_body)) / 2 : S.kind == K_PIN ? (GFX_W - gfx_text_width(l, &wl_font_body)) / 2 : 12;
          if (k && !hero) {
            char num[8];
            snprintf(num, sizeof(num), "%.*s", k, l);
            gfx_text(x, y, num, &wl_font_body, C_ACCENT);
            x += gfx_text_width("00. ", &wl_font_body);
            gfx_text(x, y, l + k + 1, &wl_font_bold, C_FG);
          } else {
            bool question = !pg && l[0] && l[strlen(l) - 1] == '?'; /* quiz / choices: the question stands out */
            bool dim = !pg && i == 0 && S.kind == K_MENU && !question;
            gfx_text(x, y, l, (hero && i == 0) || question ? &wl_font_bold : &wl_font_body, question ? C_ACCENT : dim ? C_DIM : C_FG);
          }
          y += LINE_H;
        }
      }
    }
    if (pg) page_dots();
    else draw_items(S.kind == K_PIN ? y + 8 : y + 6);
  }
  if (approve_view()) draw_decision();
  else draw_legend();
}

void ui_render(void) {
  if (!S.dirty) return;
  S.dirty = false;
  draw();
  gfx_flush();
}

/* ---------------------------------------------------------------- generic screens */
static void show_menu(const char *title, wl_tone_t tone, result_fn on) {
  screen(K_MENU, title, tone, on);
  hint("▲▼ choose  ✓ select");
}


static int line_h(const char *l) { return l[0] == '\x01' ? wl_font_big.height + 2 : LINE_H; }

/* Cut every page into views that fit between the title and the footer. */
static void build_views(void) {
  S.nviews = 0;
  for (uint8_t p = 0; p < S.rv->npages; p++) {
    const wl_page_t *pg = &S.rv->pages[p];
    uint8_t i = 0;
    do {
      if (S.nviews >= sizeof(S.vpage)) return; /* cannot happen: every line fits a view */
      uint8_t v = S.nviews++, start = i;
      int h = 0;
      int room = (S.final_ok ? FOOT_Y - 14 : FOOT_Y - 6) - BODY_Y;
      while (i < pg->n && (i == start || h + line_h(pg->lines[i]) <= room)) h += line_h(pg->lines[i++]);
      S.vpage[v] = p, S.vfirst[v] = start, S.vcount[v] = (uint8_t)(i - start);
    } while (i < pg->n);
  }
}

static void show_pages(const wl_review_t *rv, bool final_ok, result_fn on) {
  screen(K_PAGES, "", WL_TONE_NORMAL, on);
  S.rv = rv;
  S.final_ok = final_ok;
  build_views();
  hint(S.nviews > 1 ? "▼ next page  ✗ back" : final_ok ? "✓ confirm  ✗ reject" : "✓ continue");
}

/* Single info/warning page with "continue". */
static void show_info(const char *title, wl_tone_t tone, const char *const *lines, size_t n, result_fn on) {
  wl_review_init(&local_rv);
  wl_page_t *p = wl_review_page(&local_rv, title, tone);
  for (size_t i = 0; i < n; i++) wl_page_line(p, "%s", lines[i]);
  show_pages(&local_rv, false, on);
}

/* Fixed keys (1 2 3 4 5 6 / 7 8 9 0 ← ✓), but the cursor starts on a random digit and jumps to a
   random digit after every entry. Someone watching the ▲▼ presses learns nothing (the moves
   depend on an unseen start), and the keys themselves never move: easier to type than the old
   shuffled pad, which the user found too annoying. */
static void pin_layout(void) {
  static const char *const KEYS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "←", "✓"};
  S.nitems = 0;
  for (unsigned i = 0; i < sizeof(KEYS) / sizeof(KEYS[0]); i++) item(KEYS[i]);
}

static void pin_random_cursor(void) { S.cursor = (uint8_t)(random32() % 10u); /* one of the digits */ }

static void pin_lines(void) {
  char dots[2 * VAULT_PIN_MAX + 2] = "\x01";
  for (uint8_t i = 0; i < S.pinlen; i++) strcat(dots, "•");
  if (!S.pinlen) strcat(dots, "-");
  S.lines[S.nlines - 1][0] = 0;
  snprintf(S.lines[S.nlines - 1], sizeof(S.lines[0]), "%s", dots);
}

static void show_pin(const char *title, const char *info, result_fn on) {
  screen(K_PIN, title, WL_TONE_NORMAL, on);
  if (info) line(info);
  line("");
  S.cols = 6;
  pin_layout();
  pin_random_cursor();
  pin_lines();
  hint("▲▼ move  ✓ digit  ✗ delete");
}

/* ---------------------------------------------------------------- word picker */
static void word_refresh(void) {
  uint16_t m[2];
  size_t count = wl_word_complete(S.prefix, m, 2);
  uint32_t letters = wl_word_next_letters(S.prefix);
  int exact = wl_word_index(S.prefix);
  S.nitems = 0;
  S.wide_first = false;
  char buf[ITEM_LEN];
  if (S.prefix[0] && (count == 1 || (exact >= 0 && strlen(S.prefix) >= 3))) {
    snprintf(buf, sizeof(buf), "✓ %s", count == 1 ? wl_word(m[0]) : S.prefix);
    item(buf);
    S.wide_first = true;
  }
  for (int l = 0; l < 26; l++) {
    if (letters & (1u << l)) {
      char s[2] = {(char)('a' + l), 0};
      item(s);
    }
  }
  snprintf(S.lines[1], sizeof(S.lines[1]), "\x01%s_", S.prefix);
  snprintf(S.lines[2], sizeof(S.lines[2]), "%u matching word%s", (unsigned)count, count == 1 ? "" : "s");
  if (S.cursor >= S.nitems) S.cursor = 0;
}

static void show_word(const char *what, int number, int total, result_fn on) {
  char t[28];
  snprintf(t, sizeof(t), "%s %d of %d", what, number, total);
  screen(K_WORD, t, WL_TONE_NORMAL, on);
  S.nlines = 3;
  S.lines[0][0] = 0;
  S.cols = 13;
  S.picked = -1;
  word_refresh();
  hint("✓ add letter  ✗ delete");
}

/* ---------------------------------------------------------------- input handling */
void ui_button(uint8_t b) {
  S.dirty = true;
  uint8_t n = S.nitems;
  switch (S.kind) {
    case K_PAGES: {
      uint8_t last = (uint8_t)(S.nviews - 1);
      if (b == HAL_BTN_CONFIRM && S.page == last) {
        S.on(1);
        return;
      }
      if (b == HAL_BTN_BACK) {
        S.on(0);
        return;
      }
      if (b == HAL_BTN_UP && S.page > 0) S.page--;
      else if ((b == HAL_BTN_DOWN || b == HAL_BTN_CONFIRM) && S.page < last) S.page++; /* OK only counts on the LAST page */
      hint(S.page < last ? "▼ next page  ✗ back" : S.final_ok ? "✓ confirm  ✗ reject" : "✓ continue  ✗ back");
      return;
    }
    case K_MENU:
    case K_PIN:
    case K_WORD:
      if (b == HAL_BTN_UP && n) S.cursor = (uint8_t)((S.cursor + n - 1) % n);
      if (b == HAL_BTN_DOWN && n) S.cursor = (uint8_t)((S.cursor + 1) % n);
      break;
  }
  if (S.kind == K_MENU) {
    if (b == HAL_BTN_CONFIRM && n) S.on(S.cursor);
    else if (b == HAL_BTN_BACK) S.on(-1);
  } else if (S.kind == K_PIN) {
    if (b == HAL_BTN_BACK) {
      if (S.pinlen) S.pin[--S.pinlen] = 0, pin_lines();
      else S.on(0);
    } else if (b == HAL_BTN_CONFIRM) {
      const char *it = S.items[S.cursor];
      if (!strcmp(it, "←")) {
        if (S.pinlen) S.pin[--S.pinlen] = 0;
      } else if (!strcmp(it, "✓")) {
        if (S.pinlen >= VAULT_PIN_MIN) {
          S.on(1);
          return;
        }
      } else if (S.pinlen < VAULT_PIN_MAX) {
        S.pin[S.pinlen++] = it[0];
        pin_random_cursor(); /* new random start for the next digit */
      }
      pin_lines();
    }
  } else if (S.kind == K_WORD) {
    size_t pl = strlen(S.prefix);
    if (b == HAL_BTN_BACK) {
      if (pl) {
        S.prefix[pl - 1] = 0;
        word_refresh();
      } else {
        S.on(0);
      }
    } else if (b == HAL_BTN_CONFIRM && n) {
      if (S.wide_first && S.cursor == 0) {
        uint16_t m[1];
        size_t c = wl_word_complete(S.prefix, m, 1);
        S.picked = c == 1 ? m[0] : wl_word_index(S.prefix);
        S.on(1);
        return;
      }
      if (pl < 8) {
        S.prefix[pl] = S.items[S.cursor][0];
        S.prefix[pl + 1] = 0;
      }
      S.cursor = 0;
      uint16_t m[2];
      if (strlen(S.prefix) >= 4 && wl_word_complete(S.prefix, m, 2) == 1) { /* BIP39: unique after 4 letters */
        S.picked = m[0];
        S.on(1);
        return;
      }
      word_refresh();
    }
  }
}

/* ---------------------------------------------------------------- flows */
static struct {
  uint16_t words[24];      /* mnemonic being created or imported */
  size_t count;
  int edit;                /* import: index of the word being re-entered, -1 = appending */
  uint8_t quiz_pos[3];
  uint16_t quiz_opts[4];
  int quiz_k;
  bool for_sweep;          /* creating the NEW wallet of a sweep */
  uint16_t sweep_words[24]; /* imported seed waiting to become the sweep source */
  size_t sweep_count;
  char pin1[VAULT_PIN_MAX + 1];
  int after_pin;           /* what to do after a PIN check */
  int mode;                /* MODE_*: what the shared word screens are for */
  /* microSD backup / restore (the file is ciphertext; names are random) */
  uint8_t bk_file[WL_BACKUP_FILE_LEN];
  char bk_names[BK_MAX_FILES][BK_NAME_LEN];
  size_t bk_n;
  bool restore_blind;
} F;

/* F.words holds the wallet's words (MODE_WALLET), the new backup password while it is shown and
   checked (MODE_BACKUP), or the password being typed in, then the restored words (MODE_RESTORE). */
enum { MODE_WALLET = 0, MODE_BACKUP, MODE_RESTORE };

static void flow_end(void) {
  wl_wipe(&F, sizeof(F));
  wl_wipe(&local_rv, sizeof(local_rv));
  ui_home();
}

static void go_home_r(int r) {
  (void)r;
  flow_end();
}

static void status_info(const char *title, wl_tone_t tone, const char *a, const char *b, const char *c) {
  const char *l[3] = {a, b, c};
  show_info(title, tone, l, c ? 3 : b ? 2 : 1, go_home_r);
}

/* ---- choose PIN, then create the vault */
static void choose_pin(void);
static void pin_repeat_r(int r);
static void finish_create(void);

static void pin_first_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  snprintf(F.pin1, sizeof(F.pin1), "%s", S.pin);
  show_pin("Repeat PIN", "Enter the same PIN again", pin_repeat_r);
}
static void pin_mismatch_r(int r) {
  (void)r;
  choose_pin();
}
static void pin_repeat_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  size_t a = strlen(F.pin1), b = strlen(S.pin);
  if (a != b || !wl_ct_equal(F.pin1, S.pin, a)) {
    wl_wipe(F.pin1, sizeof(F.pin1));
    const char *l[] = {"The two PINs differ.", "Try again."};
    show_info("PINs differ", WL_TONE_WARN, l, 2, pin_mismatch_r);
    return;
  }
  finish_create();
}
static void choose_pin(void) { show_pin("Choose PIN", "4 to 8 digits", pin_first_r); }

static void finish_create(void) {
  vault_result_t r = vault_create(F.words, F.count, F.pin1);
  if (r == VAULT_OK && F.for_sweep) r = vault_set_sweep(F.sweep_words, F.sweep_count);
  if (r == VAULT_OK && F.mode == MODE_RESTORE && F.restore_blind) r = vault_set_blind_signing(true);
  bool sweep = F.for_sweep, restored = F.mode == MODE_RESTORE;
  wl_wipe(F.pin1, sizeof(F.pin1));
  fw_state_changed();
  if (r != VAULT_OK) {
    status_info("Error", WL_TONE_DANGER, "Could not store the", "wallet. Nothing saved.", NULL);
    return;
  }
  hal_log("wallet stored%s", sweep ? " (+ imported seed to sweep)" : restored ? " (restored from microSD)" : "");
  if (restored) {
    status_info("Restored", WL_TONE_NORMAL, "Wallet restored from", "the microSD backup.", "Unlocked.");
    return;
  }
  status_info("Ready", WL_TONE_NORMAL, sweep ? "New wallet created." : "Wallet ready.",
              sweep ? "Now sweep the old seed" : "Unlocked.", sweep ? "from the phone app." : NULL);
}

/* ---- create: show words, quiz */
static void show_words(void);
static void quiz_ask(void);
static void do_backup(void);
static void show_words_r(int r) {
  (void)r;
  show_words();
}

static void quiz_r(int r) {
  if (r < 0) {
    flow_end();
    return;
  }
  uint16_t want = F.words[F.quiz_pos[F.quiz_k]];
  if (F.quiz_opts[r] != want) {
    const char *l[] = {"That is not the right", "word. Check your backup", "and look at the words", "again."};
    show_info("Wrong word", WL_TONE_WARN, l, 4, show_words_r);
    return;
  }
  if (++F.quiz_k == 3) {
    if (F.mode == MODE_BACKUP) do_backup();
    else choose_pin();
    return;
  }
  quiz_ask();
}

static void quiz_ask(void) {
  uint8_t pos = F.quiz_pos[F.quiz_k];
  F.quiz_opts[0] = F.words[pos];
  for (int i = 1; i < 4; i++) {
    uint16_t w;
    bool dup;
    do {
      w = (uint16_t)(random32() % 2048);
      dup = false;
      for (int k = 0; k < i; k++) dup |= F.quiz_opts[k] == w;
    } while (dup);
    F.quiz_opts[i] = w;
  }
  for (int i = 3; i > 0; i--) { /* shuffle the four options */
    int j = (int)(random32() % (uint32_t)(i + 1));
    uint16_t t = F.quiz_opts[i];
    F.quiz_opts[i] = F.quiz_opts[j];
    F.quiz_opts[j] = t;
  }
  char t[28], q[32];
  snprintf(t, sizeof(t), "Check %d of 3", F.quiz_k + 1);
  show_menu(t, WL_TONE_NORMAL, quiz_r);
  snprintf(q, sizeof(q), "Which is word #%u?", pos + 1u);
  line(q);
  for (int i = 0; i < 4; i++) item(wl_word(F.quiz_opts[i]));
}

static void words_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  /* pick 3 distinct positions */
  uint8_t pos[24];
  for (uint8_t i = 0; i < F.count; i++) pos[i] = i;
  shuffle_u8(pos, F.count);
  memcpy(F.quiz_pos, pos, 3);
  F.quiz_k = 0;
  quiz_ask();
}

static void show_words(void) {
  wl_review_init(&local_rv);
  bool pw = F.mode == MODE_BACKUP;
  for (size_t i = 0; i < F.count; i += 4) {
    wl_page_t *p = wl_review_page(&local_rv, pw ? "Backup password" : "Write down", WL_TONE_NORMAL);
    for (size_t k = i; k < i + 4 && k < F.count; k++) wl_page_line(p, "%2u. %s", (unsigned)(k + 1), wl_word(F.words[k]));
  }
  if (pw) {
    wl_page_t *p = wl_review_page(&local_rv, "Keep it apart", WL_TONE_WARN);
    wl_page_line(p, "This is NOT your");
    wl_page_line(p, "recovery phrase. It only");
    wl_page_line(p, "opens this backup file.");
    wl_page_line(p, " ");
    wl_page_line(p, "Card + password = wallet:");
    wl_page_line(p, "never store them together.");
    wl_page_line(p, "Next: a quick check.");
    show_pages(&local_rv, false, words_r);
    return;
  }
  wl_page_t *p = wl_review_page(&local_rv, "Keep it safe", WL_TONE_WARN);
  wl_page_line(p, "Never type these words");
  wl_page_line(p, "into a phone or computer");
  wl_page_line(p, "or take a photo.");
  wl_page_line(p, " ");
  wl_page_line(p, "Next: a quick check.");
  show_pages(&local_rv, false, words_r);
}

static void create_words_r(int r) {
  if (r < 0) {
    flow_end();
    return;
  }
  size_t ent_len = r == 0 ? 16 : 32;
  uint8_t ent[32];
  /* Seed entropy: RP2350 TRNG + secure element RNG, health-tested (core/src/entropy.c). */
  if (!rng_collect(ent, ent_len) || wl_mnemonic_from_entropy(ent, ent_len, F.words, &F.count) != WL_OK) {
    wl_wipe(ent, sizeof(ent));
    status_info("Entropy error", WL_TONE_DANGER, "Random source failed", "its health test.", "Wallet NOT created.");
    return;
  }
  wl_wipe(ent, sizeof(ent));
  show_words();
}

static void create_flow(bool for_sweep) {
  F.for_sweep = for_sweep;
  show_menu("New wallet", WL_TONE_NORMAL, create_words_r);
  line("How many words?");
  item("12 words");
  item("24 words");
}

/* ---- import */
static void enter_word(void);
static void review_words(void);

static void word_r(int r) {
  if (!r) { /* back on an empty prefix */
    if (F.edit >= 0) {
      F.edit = -1;
      review_words();
    } else if (F.count > 0) {
      F.count--;
      enter_word();
    } else {
      flow_end();
    }
    return;
  }
  if (S.picked < 0) return;
  if (F.edit >= 0) {
    F.words[F.edit] = (uint16_t)S.picked;
    F.edit = -1;
    review_words();
    return;
  }
  F.words[F.count++] = (uint16_t)S.picked;
  enter_word();
}

static size_t import_target;

static void enter_word(void) {
  if (F.edit < 0 && F.count == import_target) {
    review_words();
    return;
  }
  show_word(F.mode == MODE_RESTORE ? "Password" : "Word", F.edit >= 0 ? F.edit + 1 : (int)F.count + 1,
            (int)import_target, word_r);
}

static void import_choice_r(int r);
static void restore_decrypt(void);

static void review_r(int r) {
  if (r < 0) {
    flow_end();
    return;
  }
  if ((size_t)r < F.count) {
    F.edit = r;
    enter_word();
    return;
  }
  /* "✓ Words are correct" */
  if (F.mode == MODE_RESTORE) {
    restore_decrypt();
    return;
  }
  show_menu("Was it ever typed?", WL_TONE_WARN, import_choice_r);
  line("A seed ever typed into a");
  line("phone or computer may");
  line("already be stolen.");
  line("Sweep is recommended.");
  item("Import & sweep (safer)");
  item("Import as-is");
  item("Cancel");
}

static void review_words(void) {
  bool ok = wl_mnemonic_check_indices(F.words, F.count) == WL_OK;
  bool pw = F.mode == MODE_RESTORE;
  show_menu(ok ? (pw ? "Review password" : "Review words") : "Checksum wrong!", ok ? WL_TONE_NORMAL : WL_TONE_DANGER,
            review_r);
  if (ok) {
    line("Check every word against");
    line(pw ? "the written password." : "your paper backup.");
  } else {
    line("One or more words are");
    line("wrong. Pick one to fix.");
  }
  for (size_t i = 0; i < F.count; i++) {
    char s[ITEM_LEN];
    snprintf(s, sizeof(s), "%2u. %s", (unsigned)(i + 1), wl_word(F.words[i]));
    item(s);
  }
  if (ok) item(pw ? "✓ Password is correct" : "✓ Words are correct");
}

static void import_count_r(int r) {
  if (r < 0) {
    flow_end();
    return;
  }
  static const uint8_t counts[] = {12, 15, 18, 21, 24};
  import_target = counts[r];
  F.count = 0;
  F.edit = -1;
  enter_word();
}

static void import_flow(void) {
#ifdef TREDDER_DEMO_NO_SE
  /* No secure element: a real seed typed here would sit in unprotected RAM. Refuse. */
  status_info("Import disabled", WL_TONE_WARN, "Demo build, no security", "chip. Never type a real", "seed into this device.");
  return;
#endif
  show_menu("Import", WL_TONE_NORMAL, import_count_r);
  line("Number of words?");
  item("12 words");
  item("15 words");
  item("18 words");
  item("21 words");
  item("24 words");
}

/* ---- PIN-confirmed actions while unlocked */
enum { AFTER_REPLACE = 1, AFTER_WIPE, AFTER_BLIND_ON, AFTER_SWEEP_EXISTING, AFTER_BACKUP };
static void backup_intro(void);

static void after_pin_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  vault_result_t v = vault_check_pin(S.pin);
  wl_wipe(S.pin, sizeof(S.pin));
  if (v == VAULT_WIPED) {
    fw_state_changed();
    status_info("Device wiped", WL_TONE_DANGER, "Too many wrong PINs.", "All keys were erased.", NULL);
    return;
  }
  if (v != VAULT_OK) {
    char left[32];
    snprintf(left, sizeof(left), "%u attempts left.", vault_tries_left());
    status_info("Wrong PIN", WL_TONE_WARN, "Nothing was changed.", left, NULL);
    return;
  }
  switch (F.after_pin) {
    case AFTER_REPLACE:
      v = vault_replace_active(F.words, F.count);
      fw_state_changed();
      status_info(v == VAULT_OK ? "Imported" : "Error", v == VAULT_OK ? WL_TONE_NORMAL : WL_TONE_DANGER,
                  v == VAULT_OK ? "The imported seed is now" : "Nothing was changed.", v == VAULT_OK ? "the wallet." : NULL,
                  NULL);
      break;
    case AFTER_SWEEP_EXISTING:
      v = vault_set_sweep(F.words, F.count);
      fw_state_changed();
      status_info(v == VAULT_OK ? "Ready to sweep" : "Error", WL_TONE_NORMAL, "Imported seed stored.",
                  "Sweep it to this wallet", "from the phone app.");
      break;
    case AFTER_WIPE:
      vault_wipe();
      fw_state_changed();
      hal_log("device wiped by user");
      status_info("Wiped", WL_TONE_NORMAL, "All keys were erased.", NULL, NULL);
      break;
    case AFTER_BLIND_ON:
      vault_set_blind_signing(true);
      flow_end();
      break;
    case AFTER_BACKUP:
      backup_intro();
      break;
    default:
      flow_end();
  }
}

static void confirm_then_pin_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  show_pin("PIN to confirm", NULL, after_pin_r);
}

static void danger_confirm(int after, const char *title, const char *a, const char *b, const char *c) {
  F.after_pin = after;
  wl_review_init(&local_rv);
  wl_page_t *p = wl_review_page(&local_rv, title, WL_TONE_DANGER);
  wl_page_line(p, "%s", a);
  if (b) wl_page_line(p, "%s", b);
  if (c) wl_page_line(p, "%s", c);
  show_pages(&local_rv, true, confirm_then_pin_r);
}

static void sweep_intro_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  /* Imported seed becomes the sweep source; a brand-new seed becomes the wallet. */
  memcpy(F.sweep_words, F.words, sizeof(F.words));
  F.sweep_count = F.count;
  wl_wipe(F.words, sizeof(F.words));
  F.count = 0;
  create_flow(true);
}

static void import_choice_r(int r) {
  if (r == 0) { /* import & sweep */
    if (vault_initialized()) {
      F.after_pin = AFTER_SWEEP_EXISTING;
      show_pin("PIN to confirm", "Store seed for sweeping", after_pin_r);
      return;
    }
    const char *l[] = {"The device now creates", "a NEW seed. Write it", "down and pass the check.", " ",
                       "Then the phone app moves", "the funds from the old", "seed to the new one."};
    show_info("Sweep mode", WL_TONE_NORMAL, l, 7, sweep_intro_r);
  } else if (r == 1) { /* import as-is */
    if (vault_initialized()) {
      danger_confirm(AFTER_REPLACE, "Replace wallet?", "The current wallet will", "be ERASED. Only continue",
                     "if it is backed up.");
      return;
    }
    choose_pin();
  } else {
    flow_end();
  }
}

/* ---- unlock */
static void unlock_pin_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  vault_result_t v = vault_unlock(S.pin);
  wl_wipe(S.pin, sizeof(S.pin));
  fw_state_changed();
  if (v == VAULT_OK) {
    hal_log("unlocked");
    flow_end();
  } else if (v == VAULT_WRONG_PIN) {
    uint8_t left = vault_tries_left(), failed = (uint8_t)(SE_PIN_MAX_TRIES - left);
    if (failed >= 3) D.lockout_until = D.now + (fw_config.pin_delay_ms << (failed - 3));
    char l1[32];
    snprintf(l1, sizeof(l1), "%u attempts left", left);
    status_info("Wrong PIN", WL_TONE_WARN, l1, "before the device wipes", "itself.");
  } else if (v == VAULT_WIPED) {
    hal_log("DEVICE WIPED: 10 wrong PINs");
    status_info("Device wiped", WL_TONE_DANGER, "10 wrong PINs.", "All keys were erased.", NULL);
  } else {
    status_info("Error", WL_TONE_DANGER, "Secure element or", "storage error.", NULL);
  }
}

static void unlock_flow(void) {
  if ((int32_t)(D.lockout_until - D.now) > 0) {
    char l[32];
    snprintf(l, sizeof(l), "Try again in %lus", (unsigned long)((D.lockout_until - D.now + 999) / 1000));
    status_info("Please wait", WL_TONE_WARN, "Too many wrong PINs.", l, NULL);
    return;
  }
  char info[32];
  snprintf(info, sizeof(info), "Attempts left: %u", vault_tries_left());
  show_pin("Enter PIN", info, unlock_pin_r);
}

/* ---------------------------------------------------------------- microSD backup + restore (M6) */
/* A screen shown while the device works (key derivation takes seconds): drawn immediately. */
static void busy(const char *title, const char *a, const char *b) {
  const char *l[2] = {a, b};
  show_info(title, WL_TONE_NORMAL, l, b ? 2 : 1, go_home_r);
  hint("please wait...");
  ui_render();
}

static void bk_fail(const char *title, bk_status_t s) {
  const char *a, *b;
  bk_status_text(s, &a, &b);
  status_info(title, WL_TONE_WARN, a, b[0] ? b : NULL, NULL);
}

static void backup_show_password_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  /* the card is there and readable: now make the password (TRNG + SE, health-tested) */
  uint8_t ent[16];
  F.mode = MODE_BACKUP;
  if (!rng_collect(ent, sizeof(ent)) || wl_mnemonic_from_entropy(ent, sizeof(ent), F.words, &F.count) != WL_OK) {
    wl_wipe(ent, sizeof(ent));
    status_info("Entropy error", WL_TONE_DANGER, "Random source failed", "its health test.", "No backup made.");
    return;
  }
  wl_wipe(ent, sizeof(ent));
  show_words();
}

static void backup_intro_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  busy("microSD", "Checking the card...", NULL);
  bk_status_t s = bk_list(F.bk_names, &F.bk_n);
  if (s != BK_OK && s != BK_NO_FILES) {
    bk_fail("Backup", s);
    return;
  }
  const char *l[] = {"Card OK.", " ", "Next: the device shows a", "new 12-word BACKUP", "PASSWORD. Have paper and",
                     "a pen ready."};
  show_info("Backup", WL_TONE_NORMAL, l, 6, backup_show_password_r);
}

static void backup_intro(void) {
  const char *l[] = {"Insert a microSD card", "(FAT32 or exFAT).", " ", "The wallet is encrypted",
                     "with a new password the", "device makes up. The", "card alone is useless."};
  show_info("Back up to microSD", WL_TONE_NORMAL, l, 7, backup_intro_r);
}

static void do_backup(void) {
  busy("Saving backup", "Encrypting, writing and", "checking. A few seconds.");
  char name[BK_NAME_LEN];
  bk_status_t s = bk_create(F.words, name);
  wl_wipe(F.words, sizeof(F.words));
  if (s != BK_OK) {
    bk_fail("Backup failed", s);
    return;
  }
  char l1[32];
  snprintf(l1, sizeof(l1), "%s", name);
  const char *l[] = {"Saved and checked as", l1, " ", "Keep the card and the", "password in different", "places."};
  show_info("Backup saved", WL_TONE_NORMAL, l, 6, go_home_r);
}

static void restore_enter_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  F.mode = MODE_RESTORE;
  F.count = 0;
  F.edit = -1;
  import_target = WL_BACKUP_PW_WORDS;
  enter_word();
}

static void restore_pick_r(int r) {
  if (r < 0 || (size_t)r >= F.bk_n) {
    flow_end();
    return;
  }
  busy("microSD", "Reading the backup...", NULL);
  bk_status_t s = bk_load(F.bk_names[r], F.bk_file);
  if (s != BK_OK) {
    bk_fail("Restore", s);
    return;
  }
  const char *l[] = {"Enter the 12-word", "BACKUP PASSWORD that was", "shown when this backup", "was made.", " ",
                     "(Not the recovery phrase.)"};
  show_info("Backup password", WL_TONE_NORMAL, l, 6, restore_enter_r);
}

static void restore_flow(void) {
  F.mode = MODE_RESTORE;
  busy("microSD", "Looking for backups...", NULL);
  bk_status_t s = bk_list(F.bk_names, &F.bk_n);
  if (s != BK_OK) {
    bk_fail("Restore", s);
    return;
  }
  if (F.bk_n == 1) {
    restore_pick_r(0);
    return;
  }
  show_menu("Choose backup", WL_TONE_NORMAL, restore_pick_r);
  line("Backups on the card:");
  for (size_t i = 0; i < F.bk_n; i++) item(F.bk_names[i]);
}

static void restore_retry_r(int r) {
  if (r != 0) {
    flow_end();
    return;
  }
  restore_enter_r(1);
}

static void restore_ready_r(int r) {
  if (!r) {
    flow_end();
    return;
  }
  choose_pin();
}

static void restore_decrypt(void) {
  busy("Decrypting", "Checking the password.", "A few seconds.");
  wl_backup_payload_t p;
  bk_status_t s = bk_decrypt(F.bk_file, F.words, &p);
  wl_wipe(F.words, sizeof(F.words));
  F.count = 0;
  if (s == BK_WRONG_PASSWORD || s == BK_PASSWORD_TYPO) {
    const char *a, *b;
    bk_status_text(s, &a, &b);
    show_menu("Wrong password", WL_TONE_WARN, restore_retry_r);
    line(a);
    line(b);
    item("Enter it again");
    item("Cancel");
    return;
  }
  if (s != BK_OK) {
    wl_wipe(&p, sizeof(p));
    bk_fail("Restore", s);
    return;
  }
  memcpy(F.words, p.idx, sizeof(F.words));
  F.count = p.words;
  F.restore_blind = p.blind_signing;
  wl_wipe(&p, sizeof(p));
  hal_log("restore: backup decrypted (%u words)", (unsigned)F.count);
  char l1[32];
  snprintf(l1, sizeof(l1), "Wallet with %u words found.", (unsigned)F.count);
  const char *l[] = {"Backup decrypted.", l1, " ", "Now choose a PIN for", "this device."};
  show_info("Backup OK", WL_TONE_NORMAL, l, 5, restore_ready_r);
}

/* ---------------------------------------------------------------- top-level screens */
static void setup_settings(void);
static void setup_r(int r) {
  if (r == 0) create_flow(false);
  else if (r == 1) import_flow();
  else if (r == 2) restore_flow();
  else if (r == 3) setup_settings();
}

static void locked_r(int r) {
  if (r == 0) unlock_flow();
}

static void blind_r(int r) {
  if (r) {
    F.after_pin = AFTER_BLIND_ON;
    show_pin("PIN to confirm", "Enable blind signing", after_pin_r);
  } else {
    flow_end();
  }
}

static void settings(void);
static void battery_screen(bool from_setup);
static void log_screen(bool from_setup);
static void setup_settings(void);
static void unpair_r(int r) {
  if (r) {
    bool had = proto_paired();
    proto_forget_pairing();
    hal_log("phone unpaired by the user");
    status_info("Unpaired", WL_TONE_NORMAL, had ? "The phone app is no longer" : "No phone was paired.", had ? "paired. To use it again," : NULL,
                had ? "pair and compare the code." : NULL);
    return;
  }
  flow_end();
}

static void home_r(int r) {
  switch (r) {
    case 0:
      F.after_pin = AFTER_BACKUP;
      show_pin("PIN to confirm", "Back up to microSD", after_pin_r);
      break;
    case 1: {
      wl_review_init(&local_rv);
      wl_page_t *p = wl_review_page(&local_rv, "Unpair phone?", WL_TONE_WARN);
      wl_page_line(p, "The phone app loses access.");
      wl_page_line(p, "Pairing again needs this");
      wl_page_line(p, "device, the PIN and the code.");
      show_pages(&local_rv, true, unpair_r);
      break;
    }
    case 2:
      fw_lock("user");
      break;
    case 3:
      settings();
      break;
    default:
      break;
  }
}

static void settings_r(int r) {
  switch (r) {
    case 0:
      if (vault_blind_signing()) {
        vault_set_blind_signing(false);
        ui_home();
      } else {
        wl_review_init(&local_rv);
        wl_page_t *p = wl_review_page(&local_rv, "Blind signing?", WL_TONE_DANGER);
        wl_page_line(p, "You would sign data the");
        wl_page_line(p, "device cannot decode or");
        wl_page_line(p, "show you. Scammers rely");
        wl_page_line(p, "on exactly this.");
        wl_page_line(p, " ");
        wl_page_line(p, "Leave it OFF unless you");
        wl_page_line(p, "know why you need it.");
        show_pages(&local_rv, true, blind_r);
      }
      break;
    case 1:
      import_flow();
      break;
    case 2:
      danger_confirm(AFTER_WIPE, "Wipe device?", "Erases the seed.", "Only your written backup", "can restore the funds.");
      break;
    case 3:
      battery_screen(false);
      break;
    case 4:
      log_screen(false);
      break;
    case 5: {
#ifdef TREDDER_DEMO_NO_SE
      const char *b = "Demo build: no security";
      const char *c = "chip, nothing is saved.";
#else
      const char *b = "Keys in the NXP SE050";
      const char *c = "secure element.";
#endif
      status_info("About", WL_TONE_NORMAL, "tredder " FW_VERSION, b, c);
      break;
    }
    default:
      flow_end(); /* back */
      break;
  }
}

static void settings(void) {
  show_menu("Settings", WL_TONE_NORMAL, settings_r);
  item(vault_blind_signing() ? "Blind signing: ON" : "Blind signing: off");
  item("Import seed phrase");
  item("Wipe device");
  item("Battery");
  item("Log");
  item("About");
}

/* ---- Settings > Battery (from the Setup page or the main page's Settings) */
static bool batt_from_setup;
static void setup_settings(void);

static void battery_r(int r) {
  if (r >= BATT_BAR && r <= BATT_BOTH) {
    D.batt_mode = (uint8_t)r;
    battery_screen(batt_from_setup);
    S.cursor = (uint8_t)r;
    return;
  }
  if (batt_from_setup) setup_settings();
  else settings();
}

static void battery_screen(bool from_setup) {
  batt_from_setup = from_setup;
  show_menu("Battery", WL_TONE_NORMAL, battery_r);
  S.layout = L_BATTERY;
  hint("▲▼ choose  ✓ select  ✗ back");
  static const char *const MODES[] = {"Show: bar", "Show: percent", "Show: both"};
  for (int i = 0; i < 3; i++) {
    char s[ITEM_LEN];
    snprintf(s, sizeof(s), "%s%s", i == D.batt_mode ? "✓ " : "", MODES[i]);
    item(s);
  }
  S.cursor = D.batt_mode;
}

static void about(void) {
#ifdef TREDDER_DEMO_NO_SE
  status_info("About", WL_TONE_NORMAL, "tredder " FW_VERSION, "Demo build: no security", "chip, nothing is saved.");
#else
  status_info("About", WL_TONE_NORMAL, "tredder " FW_VERSION, "Keys in the NXP SE050", "secure element.");
#endif
}

static bool log_from_setup;
static void log_r(int r) {
  (void)r; /* the only input is Back */
  if (log_from_setup) setup_settings();
  else settings();
}

static void log_screen(bool from_setup) {
  log_from_setup = from_setup;
  show_menu("Log", WL_TONE_NORMAL, log_r);
  S.layout = L_LOG;
  hint("✗ back");
}

static void setup_settings_r(int r) {
  if (r == 0) battery_screen(true);
  else if (r == 1) log_screen(true);
  else if (r == 2) about();
  else flow_end(); /* back to Setup */
}

static void setup_settings(void) {
  show_menu("Settings", WL_TONE_NORMAL, setup_settings_r);
  item("Battery");
  item("Log");
  item("About");
}

void ui_set_balance(const char *text) {
  snprintf(D.balance, sizeof(D.balance), "%s", text ? text : "");
  if (S.layout == L_HOME) S.dirty = true;
}

void ui_home(void) {
  if (!vault_initialized()) {
    show_menu("Setup", WL_TONE_NORMAL, setup_r);
    S.layout = L_WELCOME;
#ifdef TREDDER_DEMO_NO_SE
    line("DEMO, no security chip:");
    line("nothing is saved.");
#endif
    item("New wallet");
    item("Import wallet");
    item("Restore from microSD");
    item("Settings");
  } else if (!vault_unlocked()) {
    show_menu("tredder", WL_TONE_NORMAL, locked_r);
    S.layout = L_LOCKED;
    hint("✓ unlock");
    line("Locked");
    if (D.app_connected && !D.paired) {
      line("A phone wants to pair:");
      line("unlock to see the code.");
    }
    uint8_t left = vault_tries_left();
    if (left < SE_PIN_MAX_TRIES) {
      char l[32];
      snprintf(l, sizeof(l), "%u wrong PIN attempt(s)", (unsigned)(SE_PIN_MAX_TRIES - left));
      line(l);
    }
    item("Unlock");
  } else {
    char title[28];
    snprintf(title, sizeof(title), "tredder v%s", FW_VERSION);
    char *dash = strchr(title, '-'); /* "0.3.0-demo" -> "0.3.0" (the About screen has the rest) */
    if (dash) *dash = 0;
    show_menu(title, WL_TONE_NORMAL, home_r);
    S.layout = L_HOME;
    line("Unlocked");
    line(D.app_connected ? (D.paired ? "App connected, paired" : "App connected") : "No app connected");
    if (vault_has_sweep()) line("Old seed waiting for sweep");
    item("Backup");
    item("Unpair");
    item("Lock");
    item("Settings");
  }
}

bool ui_idle(void) {
  return S.kind == K_MENU && (S.on == setup_r || S.on == locked_r || S.on == home_r);
}

/* ---------------------------------------------------------------- protocol prompts */
static ui_done_fn prompt_done;
static bool prompt_pin;

static void prompt_finish(bool ok) {
  ui_done_fn d = prompt_done;
  prompt_done = NULL;
  if (d) d(ok);
}

static void prompt_pin_r(int r) {
  if (!r) {
    prompt_finish(false);
    return;
  }
  vault_result_t v = vault_check_pin(S.pin);
  wl_wipe(S.pin, sizeof(S.pin));
  if (v == VAULT_WIPED) {
    prompt_finish(false);
    fw_state_changed();
    return;
  }
  prompt_finish(v == VAULT_OK);
}

static void prompt_pages_r(int r) {
  if (r && prompt_pin) {
    show_pin("PIN to approve", NULL, prompt_pin_r);
    return;
  }
  prompt_finish(r != 0);
}

void ui_prompt(const wl_review_t *rv, bool need_pin, ui_done_fn done) {
  prompt_done = done;
  prompt_pin = need_pin;
  show_pages(rv, true, prompt_pages_r);
}

void ui_cancel_prompt(void) {
  if (prompt_done) {
    prompt_done = NULL;
    ui_home();
  }
}

void ui_set_status(bool app_connected, bool paired) {
  if (D.app_connected != app_connected || D.paired != paired) {
    D.app_connected = app_connected, D.paired = paired;
    if (ui_idle()) ui_home();
    S.dirty = true;
  }
}

void ui_tick(uint32_t now) {
  /* charging animation: redraw every step (only the changed rows go to the panel) */
  if (D.charging && D.battery >= 0 && now / 450u != D.now / 450u) S.dirty = true;
  if (S.layout == L_LOG && now / 2000u != D.now / 2000u) S.dirty = true;
  D.now = now;
}

void ui_set_battery_diag(uint8_t state, uint16_t raw_mv) {
  if (state == D.batt_state && raw_mv / 50u == D.batt_raw_mv / 50u) return;
  D.batt_state = state;
  D.batt_raw_mv = raw_mv;
  if (S.layout == L_BATTERY) S.dirty = true;
}

void ui_set_battery_mv(uint16_t mv) {
  if (mv / 10u == D.batt_mv / 10u) return;
  D.batt_mv = mv;
  if (S.layout == L_BATTERY) S.dirty = true;
}

void ui_set_battery(int percent, bool charging) {
  int8_t p = (int8_t)(percent < 0 ? -1 : percent > 100 ? 100 : percent);
  if (p == D.battery && charging == D.charging) return;
  D.battery = p;
  D.charging = charging;
  S.dirty = true;
}

void ui_redraw(void) { S.dirty = true; }

static void battery_low_r(int r) {
  (void)r;
  ui_home();
}

void ui_battery_low(void) {
  static const char *const L[] = {"Charge the device now.", "", "It goes to sleep in a few", "seconds to protect the cell."};
  if (!ui_idle()) return; /* never interrupt a flow; the sleep follows anyway */
  show_info("Battery low", WL_TONE_WARN, L, sizeof(L) / sizeof(L[0]), battery_low_r);
}

void ui_init(void) {
  memset(&D, 0, sizeof(D));
  D.battery = -1;
  ui_home();
}

#ifdef WL_DEBUGLINK
void ui_debug(ui_debug_t *o) {
  static const char *const KIND[] = {"menu", "pages", "pin", "word"};
  memset(o, 0, sizeof(*o));
  o->kind = KIND[S.kind];
  o->cursor = S.cursor;
  if (S.kind == K_PAGES) {
    const wl_page_t *p = &S.rv->pages[S.vpage[S.page]];
    o->title = p->title;
    o->page = S.page;
    o->npages = S.nviews;
    o->nlines = S.vcount[S.page];
    for (uint8_t i = 0; i < o->nlines; i++) {
      const char *l = p->lines[S.vfirst[S.page] + i];
      o->lines[i] = l[0] == '\x01' ? l + 1 : l;
    }
  } else {
    o->title = S.title;
    o->nlines = S.nlines;
    for (uint8_t i = 0; i < S.nlines; i++) o->lines[i] = S.lines[i][0] == '\x01' ? S.lines[i] + 1 : S.lines[i];
    o->nitems = S.nitems;
    for (uint8_t i = 0; i < S.nitems; i++) o->items[i] = S.items[i];
  }
}
#endif
