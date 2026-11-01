#ifndef WALLET_REVIEW_H
#define WALLET_REVIEW_H

/*
 * What the user must see before approving something: a list of pages of
 * short text lines, produced by the transaction analysers (btc.c, evm.c,
 * sol.c) and rendered by the UI. The UI only accepts Confirm on the LAST
 * page, so every page is seen.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wallet/address.h"

#define WL_REVIEW_MAX_PAGES 16
#define WL_REVIEW_MAX_LINES 11
#define WL_REVIEW_LINE 25 /* 24 characters of the 240 px wide body font + NUL */
#define WL_ADDR_CHUNK 21  /* addresses are split into groups of this many characters */

typedef enum { WL_TONE_NORMAL = 0, WL_TONE_WARN, WL_TONE_DANGER } wl_tone_t;

typedef struct {
  char title[28];
  uint8_t tone;
  uint8_t n;
  char lines[WL_REVIEW_MAX_LINES][WL_REVIEW_LINE];
} wl_page_t;

#define WL_MAX_RECIPIENTS 8

/* Analysis result: pages to show + destinations (for sweep plan matching). */
typedef struct {
  wl_page_t pages[WL_REVIEW_MAX_PAGES];
  uint8_t npages;
  bool overflow; /* too much to show: the request must be refused */
  uint8_t nrecipients;
  char recipients[WL_MAX_RECIPIENTS][WL_ADDRESS_MAX_LEN];
} wl_review_t;

/* Why a request is refused (maps to protocol error codes). */
typedef enum { WL_VERDICT_OK = 0, WL_VERDICT_INVALID, WL_VERDICT_UNSUPPORTED } wl_verdict_t;

void wl_review_init(wl_review_t *r);
/* New page; on overflow returns a scratch page and sets r->overflow. */
wl_page_t *wl_review_page(wl_review_t *r, const char *title, wl_tone_t tone);
void wl_page_line(wl_page_t *p, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* Long value (address, hex) split over several lines. */
void wl_page_wrap(wl_page_t *p, const char *s, size_t chunk);
bool wl_review_add_recipient(wl_review_t *r, const char *addr);

/* Fixed-point decimal: digits of a big-endian unsigned integer (<= 32 bytes)
   with `decimals` decimals, trailing zeros trimmed: 1500000 / 6 -> "1.5". */
void wl_format_units(const uint8_t *be, size_t len, unsigned decimals, char *out, size_t out_size);
void wl_format_u64(uint64_t v, unsigned decimals, char *out, size_t out_size);

#endif
