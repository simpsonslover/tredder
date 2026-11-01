#include "wallet/review.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void wl_review_init(wl_review_t *r) { memset(r, 0, sizeof(*r)); }

wl_page_t *wl_review_page(wl_review_t *r, const char *title, wl_tone_t tone) {
  static wl_page_t scratch;
  wl_page_t *p;
  if (r->npages >= WL_REVIEW_MAX_PAGES) {
    r->overflow = true;
    p = &scratch;
  } else {
    p = &r->pages[r->npages++];
  }
  memset(p, 0, sizeof(*p));
  snprintf(p->title, sizeof(p->title), "%s", title);
  p->tone = (uint8_t)tone;
  return p;
}

void wl_page_line(wl_page_t *p, const char *fmt, ...) {
  if (p->n >= WL_REVIEW_MAX_LINES) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(p->lines[p->n++], WL_REVIEW_LINE, fmt, ap);
  va_end(ap);
}

void wl_page_wrap(wl_page_t *p, const char *s, size_t chunk) {
  if (chunk == 0 || chunk >= WL_REVIEW_LINE) chunk = WL_REVIEW_LINE - 1;
  size_t len = strlen(s);
  for (size_t i = 0; i < len && p->n < WL_REVIEW_MAX_LINES; i += chunk) {
    size_t n = len - i < chunk ? len - i : chunk;
    memcpy(p->lines[p->n], s + i, n);
    p->lines[p->n][n] = 0;
    p->n++;
  }
}

bool wl_review_add_recipient(wl_review_t *r, const char *addr) {
  if (r->nrecipients >= WL_MAX_RECIPIENTS) {
    r->overflow = true;
    return false;
  }
  snprintf(r->recipients[r->nrecipients++], WL_ADDRESS_MAX_LEN, "%s", addr);
  return true;
}

void wl_format_units(const uint8_t *be, size_t len, unsigned decimals, char *out, size_t out_size) {
  /* Repeated division by 10 on 32-bit limbs (little-endian limb order). */
  uint32_t limb[8] = {0};
  if (len > 32) len = 32;
  if (decimals > 40) decimals = 40;
  for (size_t i = 0; i < len; i++) {
    size_t bit = (len - 1 - i) * 8;
    limb[bit / 32] |= (uint32_t)be[i] << (bit % 32);
  }
  char digits[80];
  size_t nd = 0;
  for (;;) {
    int zero = 1;
    uint64_t rem = 0;
    for (int i = 7; i >= 0; i--) {
      uint64_t cur = (rem << 32) | limb[i];
      limb[i] = (uint32_t)(cur / 10);
      rem = cur % 10;
      if (limb[i]) zero = 0;
    }
    digits[nd++] = (char)('0' + rem);
    if (zero) break;
  }
  while (nd <= decimals) digits[nd++] = '0'; /* at least one digit before the point */
  char tmp[96];
  size_t t = 0;
  for (size_t i = nd; i-- > 0;) {
    tmp[t++] = digits[i];
    if (i == decimals && decimals) tmp[t++] = '.';
  }
  tmp[t] = 0;
  if (decimals) { /* trim trailing zeros and a dangling point */
    while (t && tmp[t - 1] == '0') tmp[--t] = 0;
    if (t && tmp[t - 1] == '.') tmp[--t] = 0;
  }
  snprintf(out, out_size, "%s", tmp);
}

void wl_format_u64(uint64_t v, unsigned decimals, char *out, size_t out_size) {
  uint8_t be[8];
  for (int i = 0; i < 8; i++) be[i] = (uint8_t)(v >> (56 - 8 * i));
  wl_format_units(be, 8, decimals, out, out_size);
}
