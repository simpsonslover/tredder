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
#include <string.h>

#include "app/firmware.h"
#include "app/gfx.h"
#include "app/hal.h"
#include "app/rng.h"
#include "app/se.h"
#include "app/vault.h"
#include "rand.h"
#include "wallet/mnemonic.h"
#include "wallet/secure.h"

/* ---------------------------------------------------------------- look */
#define C_BG RGB565(10, 12, 16)
#define C_FG RGB565(236, 239, 243)
#define C_DIM RGB565(130, 140, 155)
#define C_ACCENT RGB565(37, 99, 235)
#define C_CELL RGB565(30, 36, 46)
#define C_TITLE RGB565(30, 38, 52)
#define C_WARN RGB565(176, 108, 18)
#define C_DANGER RGB565(180, 36, 36)
#define C_OK RGB565(22, 128, 72)

#define TITLE_H 30
#define BODY_Y 38
#define LINE_H 18
#define FOOT_Y 218
#define ROW_H 24

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
} S;

static struct {
  bool app_connected, paired;
  int8_t battery; /* -1 unknown */
  bool charging;
  uint32_t lockout_until;
  uint32_t now;
} D;

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

/* ---------------------------------------------------------------- drawing */
static uint16_t tone_color(uint8_t t) { return t == WL_TONE_DANGER ? C_DANGER : t == WL_TONE_WARN ? C_WARN : C_TITLE; }

static void draw_items(int y) {
  if (!S.nitems) return;
  if (S.cols == 1) {
    int rows = (FOOT_Y - 4 - y) / ROW_H;
    if (rows < 1) rows = 1;
    if (S.cursor < S.scroll) S.scroll = S.cursor;
    if (S.cursor >= S.scroll + rows) S.scroll = (uint8_t)(S.cursor - rows + 1);
    for (int i = S.scroll; i < S.nitems && i < S.scroll + rows; i++) {
      int ry = y + (i - S.scroll) * ROW_H;
      bool sel = i == S.cursor;
      if (sel) gfx_fill(6, ry, GFX_W - 12, ROW_H - 2, C_ACCENT);
      gfx_text(14, ry + 3, S.items[i], sel ? &wl_font_bold : &wl_font_body, sel ? C_FG : C_FG);
    }
    if (S.scroll > 0) gfx_text(GFX_W - 16, y - 16, "▲", &wl_font_body, C_DIM);
    if (S.scroll + rows < S.nitems) gfx_text(GFX_W - 16, FOOT_Y - 18, "▼", &wl_font_body, C_DIM);
    return;
  }
  /* grid */
  int first = 0;
  if (S.wide_first) {
    bool sel = S.cursor == 0;
    gfx_fill(6, y, GFX_W - 12, 28, sel ? C_OK : C_CELL);
    gfx_text(14, y + 5, S.items[0], &wl_font_bold, C_FG);
    y += 32;
    first = 1;
  }
  int cw = (GFX_W - 12) / S.cols, ch = S.kind == K_PIN ? 40 : 28;
  const wl_font_t *f = S.kind == K_PIN ? &wl_font_title : &wl_font_bold;
  for (int i = first; i < S.nitems; i++) {
    int k = i - first, cx = 6 + (k % S.cols) * cw, cy = y + (k / S.cols) * ch;
    bool sel = i == S.cursor;
    gfx_fill(cx + 2, cy + 2, cw - 4, ch - 4, sel ? C_ACCENT : C_CELL);
    int tw = gfx_text_width(S.items[i], f);
    gfx_text(cx + (cw - tw) / 2, cy + (ch - f->height) / 2, S.items[i], f, C_FG);
  }
}

/* 24 x 12 battery outline + nub, fill by level; green bar while charging. */
static void draw_battery(int x, int y) {
  if (D.battery < 0) return;
  const int w = 22, h = 12;
  uint16_t c = D.charging ? C_OK : D.battery <= 15 ? C_DANGER : C_FG;
  gfx_fill(x, y, w, 1, C_FG);
  gfx_fill(x, y + h - 1, w, 1, C_FG);
  gfx_fill(x, y, 1, h, C_FG);
  gfx_fill(x + w - 1, y, 1, h, C_FG);
  gfx_fill(x + w, y + 3, 2, h - 6, C_FG);
  int fw = (w - 4) * (D.battery > 100 ? 100 : D.battery) / 100;
  if (fw < 1 && D.battery > 0) fw = 1;
  gfx_fill(x + 2, y + 2, fw, h - 4, c);
}

static void draw(void) {
  gfx_fill(0, 0, GFX_W, GFX_H, C_BG);
  uint8_t tone = S.tone;
  const wl_page_t *pg = NULL;
  if (S.kind == K_PAGES) {
    pg = &S.rv->pages[S.vpage[S.page]];
    tone = pg->tone;
  }
  gfx_fill(0, 0, GFX_W, TITLE_H, tone_color(tone));
  char title[40];
  if (pg && S.nviews > 1) snprintf(title, sizeof(title), "%s  %u/%u", pg->title, S.page + 1u, S.nviews);
  else snprintf(title, sizeof(title), "%s", pg ? pg->title : S.title);
  gfx_text(10, (TITLE_H - wl_font_title.height) / 2 + 1, title, &wl_font_title, C_FG);
  /* status dot: app link */
  gfx_fill(GFX_W - 14, 11, 8, 8, D.app_connected ? (D.paired ? C_OK : C_WARN) : C_DIM);
  draw_battery(GFX_W - 48, 9);

  int y = BODY_Y;
  uint8_t first = pg ? S.vfirst[S.page] : 0, n = pg ? S.vcount[S.page] : S.nlines;
  for (uint8_t i = 0; i < n; i++) {
    const char *l = pg ? pg->lines[first + i] : S.lines[i];
    if (l[0] == '\x01') { /* big line (codes, PIN dots) */
      int w = gfx_text_width(l + 1, &wl_font_big);
      gfx_text((GFX_W - w) / 2, y, l + 1, &wl_font_big, C_FG);
      y += wl_font_big.height + 2;
    } else {
      gfx_text(8, y, l, &wl_font_body, C_FG);
      y += LINE_H;
    }
  }
  draw_items(y + 6);
  if (S.hint[0]) {
    int w = gfx_text_width(S.hint, &wl_font_body);
    gfx_text((GFX_W - w) / 2, FOOT_Y + 2, S.hint, &wl_font_body, C_DIM);
  }
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

static wl_review_t local_rv; /* pages built by flows (not protocol prompts) */

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
      while (i < pg->n && (i == start || h + line_h(pg->lines[i]) <= FOOT_Y - BODY_Y)) h += line_h(pg->lines[i++]);
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

static void pin_layout(void) {
  uint8_t d[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  shuffle_u8(d, 10); /* randomised digit positions: a shoulder-surfer learns nothing from cursor moves */
  S.nitems = 0;
  for (int i = 0; i < 10; i++) {
    char s[2] = {(char)('0' + d[i]), 0};
    item(s);
  }
  item("←");
  item("✓");
}

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
  pin_lines();
  hint("Digit positions shuffle");
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

static void show_word(int number, int total, result_fn on) {
  char t[28];
  snprintf(t, sizeof(t), "Word %d of %d", number, total);
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
        pin_layout(); /* reshuffle after every digit */
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
} F;

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
  bool sweep = F.for_sweep;
  wl_wipe(F.pin1, sizeof(F.pin1));
  fw_state_changed();
  if (r != VAULT_OK) {
    status_info("Error", WL_TONE_DANGER, "Could not store the", "wallet. Nothing saved.", NULL);
    return;
  }
  hal_log("wallet stored%s", sweep ? " (+ imported seed to sweep)" : "");
  status_info("Ready", WL_TONE_NORMAL, sweep ? "New wallet created." : "Wallet ready.",
              sweep ? "Now sweep the old seed" : "Unlocked.", sweep ? "from the phone app." : NULL);
}

/* ---- create: show words, quiz */
static void show_words(void);
static void quiz_ask(void);
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
    choose_pin();
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
  for (size_t i = 0; i < F.count; i += 4) {
    wl_page_t *p = wl_review_page(&local_rv, "Write down", WL_TONE_NORMAL);
    for (size_t k = i; k < i + 4 && k < F.count; k++) wl_page_line(p, "%2u. %s", (unsigned)(k + 1), wl_word(F.words[k]));
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
  show_word(F.edit >= 0 ? F.edit + 1 : (int)F.count + 1, (int)import_target, word_r);
}

static void import_choice_r(int r);

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
  show_menu(ok ? "Review words" : "Checksum wrong!", ok ? WL_TONE_NORMAL : WL_TONE_DANGER, review_r);
  if (ok) {
    line("Check every word against");
    line("your paper backup.");
  } else {
    line("One or more words are");
    line("wrong. Pick one to fix.");
  }
  for (size_t i = 0; i < F.count; i++) {
    char s[ITEM_LEN];
    snprintf(s, sizeof(s), "%2u. %s", (unsigned)(i + 1), wl_word(F.words[i]));
    item(s);
  }
  if (ok) item("✓ Words are correct");
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
enum { AFTER_REPLACE = 1, AFTER_WIPE, AFTER_BLIND_ON, AFTER_SWEEP_EXISTING };

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

/* ---------------------------------------------------------------- top-level screens */
static void setup_r(int r) {
  if (r == 0) create_flow(false);
  else if (r == 1) import_flow();
  else if (r == 2) status_info("microSD restore", WL_TONE_NORMAL, "Encrypted microSD backup", "arrives in firmware", "milestone M6.");
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

static void home_r(int r) {
  switch (r) {
    case 0:
      fw_lock("user");
      break;
    case 1:
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
    case 2:
      import_flow();
      break;
    case 3:
      danger_confirm(AFTER_WIPE, "Wipe device?", "Erases the seed.", "Only your written backup", "can restore the funds.");
      break;
    default:
      break;
  }
}

void ui_home(void) {
  if (!vault_initialized()) {
    show_menu("Welcome", WL_TONE_NORMAL, setup_r);
#ifdef TREDDER_DEMO_NO_SE
    line("DEMO: no security chip.");
    line("Nothing is saved. Test only.");
#else
    line("New device");
#endif
    item("Create new wallet");
    item("Import seed phrase");
    item("Restore microSD backup");
  } else if (!vault_unlocked()) {
    show_menu("tredder", WL_TONE_NORMAL, locked_r);
    line("Locked");
    uint8_t left = vault_tries_left();
    if (left < SE_PIN_MAX_TRIES) {
      char l[32];
      snprintf(l, sizeof(l), "%u wrong PIN attempt(s)", (unsigned)(SE_PIN_MAX_TRIES - left));
      line(l);
    }
    item("Unlock");
  } else {
    show_menu("tredder", WL_TONE_NORMAL, home_r);
    line("Unlocked");
    line(D.app_connected ? (D.paired ? "App connected, paired" : "App connected") : "No app connected");
    if (vault_has_sweep()) {
      line("Imported seed waiting");
      line("to be swept");
    }
    item("Lock");
    item(vault_blind_signing() ? "Blind signing: ON" : "Blind signing: off");
    item("Import seed phrase");
    item("Wipe device");
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

void ui_tick(uint32_t now) { D.now = now; }

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
