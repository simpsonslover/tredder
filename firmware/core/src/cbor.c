#include "wallet/cbor.h"

#include <string.h>

#define MAX_DEPTH 8

/* Read the head of an item: major type, argument, header size. */
static bool head(const uint8_t *p, size_t n, uint8_t *major, uint64_t *arg, size_t *hlen) {
  if (n < 1) return false;
  uint8_t ai = p[0] & 31;
  *major = p[0] >> 5;
  size_t extra = ai < 24 ? 0 : ai == 24 ? 1 : ai == 25 ? 2 : ai == 26 ? 4 : ai == 27 ? 8 : 99;
  if (extra == 99 || n < 1 + extra) return false; /* reserved or indefinite length: rejected */
  uint64_t v = ai < 24 ? ai : 0;
  for (size_t i = 0; i < extra; i++) v = (v << 8) | p[1 + i];
  *arg = v;
  *hlen = 1 + extra;
  return true;
}

static bool skip(const uint8_t *p, size_t n, int depth, size_t *size) {
  uint8_t major;
  uint64_t arg;
  size_t h;
  if (depth > MAX_DEPTH || !head(p, n, &major, &arg, &h)) return false;
  switch (major) {
    case 0:
    case 1:
      *size = h;
      return true;
    case 2:
    case 3:
      if (arg > n - h) return false;
      *size = h + (size_t)arg;
      return true;
    case 4:
    case 5: {
      uint64_t items = major == 5 ? arg * 2 : arg;
      if (arg > n || items > n) return false; /* every item takes >= 1 byte */
      size_t off = h;
      for (uint64_t i = 0; i < items; i++) {
        size_t s;
        if (!skip(p + off, n - off, depth + 1, &s)) return false;
        off += s;
      }
      *size = off;
      return true;
    }
    case 6: {
      size_t s;
      if (!skip(p + h, n - h, depth + 1, &s)) return false;
      *size = h + s;
      return true;
    }
    default: /* 7: simple values and floats; header already holds the payload */
      *size = h;
      return true;
  }
}

bool wl_cbor_parse(const uint8_t *buf, size_t len, wl_cbor_t *root) {
  size_t s;
  if (!buf || !skip(buf, len, 0, &s) || s != len) return false;
  root->p = buf;
  root->n = len;
  return true;
}

bool wl_cbor_is_map(wl_cbor_t v) { return v.n && (v.p[0] >> 5) == 5; }

bool wl_cbor_text(wl_cbor_t v, const char **s, size_t *n) {
  uint8_t major;
  uint64_t arg;
  size_t h;
  if (!head(v.p, v.n, &major, &arg, &h) || major != 3) return false;
  *s = (const char *)v.p + h;
  *n = (size_t)arg;
  return true;
}

bool wl_cbor_text_eq(wl_cbor_t v, const char *s) {
  const char *t;
  size_t n;
  return wl_cbor_text(v, &t, &n) && n == strlen(s) && memcmp(t, s, n) == 0;
}

bool wl_cbor_map_get(wl_cbor_t map, const char *key, wl_cbor_t *val) {
  uint8_t major;
  uint64_t pairs;
  size_t h;
  if (!head(map.p, map.n, &major, &pairs, &h) || major != 5) return false;
  size_t off = h;
  for (uint64_t i = 0; i < pairs; i++) {
    size_t ks, vs;
    if (!skip(map.p + off, map.n - off, 1, &ks)) return false;
    wl_cbor_t k = {map.p + off, ks};
    off += ks;
    if (!skip(map.p + off, map.n - off, 1, &vs)) return false;
    if (wl_cbor_text_eq(k, key)) {
      /* null (0xf6) and undefined (0xf7) mean "absent" */
      if (vs == 1 && (map.p[off] == 0xf6 || map.p[off] == 0xf7)) return false;
      val->p = map.p + off;
      val->n = vs;
      return true;
    }
    off += vs;
  }
  return false;
}

static bool double_to_u64(uint64_t bits, uint64_t *out) {
  int sign = (int)(bits >> 63);
  int exp = (int)((bits >> 52) & 0x7FF);
  uint64_t frac = bits & ((1ull << 52) - 1);
  if (exp == 0 && frac == 0) {
    *out = 0;
    return true;
  }
  if (sign || exp < 1023 || exp > 1023 + 52) return false; /* negative, fraction, or > 2^53 */
  int shift = exp - 1023;
  uint64_t mant = frac | (1ull << 52);
  if (shift < 52 && (mant & ((1ull << (52 - shift)) - 1))) return false; /* not integral */
  *out = mant >> (52 - shift);
  return true;
}

bool wl_cbor_uint(wl_cbor_t v, uint64_t *out) {
  uint8_t major;
  uint64_t arg;
  size_t h;
  if (!head(v.p, v.n, &major, &arg, &h)) return false;
  if (major == 0) {
    *out = arg;
    return true;
  }
  if (major == 7 && (v.p[0] & 31) == 27) return double_to_u64(arg, out);
  if (major == 7 && (v.p[0] & 31) == 26) { /* float32 -> widen */
    uint32_t f = (uint32_t)arg;
    uint64_t sign = (uint64_t)(f >> 31), e = (f >> 23) & 0xFF, m = f & 0x7FFFFF;
    if (e == 0 && m == 0) {
      *out = 0;
      return !sign;
    }
    if (e == 0 || e == 0xFF) return false;
    uint64_t d = (sign << 63) | ((e - 127 + 1023) << 52) | (m << 29);
    return double_to_u64(d, out);
  }
  return false;
}

bool wl_cbor_bytes(wl_cbor_t v, const uint8_t **b, size_t *n) {
  uint8_t major;
  uint64_t arg;
  size_t h;
  if (!head(v.p, v.n, &major, &arg, &h)) return false;
  if (major == 6) { /* cbor-x wraps Uint8Array in tag 64 */
    if (arg != 64) return false;
    v.p += h;
    v.n -= h;
    if (!head(v.p, v.n, &major, &arg, &h)) return false;
  }
  if (major != 2) return false;
  *b = v.p + h;
  *n = (size_t)arg;
  return true;
}

bool wl_cbor_array(wl_cbor_t v, size_t *count) {
  uint8_t major;
  uint64_t arg;
  size_t h;
  if (!head(v.p, v.n, &major, &arg, &h) || major != 4) return false;
  *count = (size_t)arg;
  return true;
}

bool wl_cbor_array_at(wl_cbor_t v, size_t i, wl_cbor_t *item) {
  uint8_t major;
  uint64_t count;
  size_t h;
  if (!head(v.p, v.n, &major, &count, &h) || major != 4 || i >= count) return false;
  size_t off = h, s = 0;
  for (size_t k = 0; k <= i; k++) {
    if (!skip(v.p + off, v.n - off, 1, &s)) return false;
    if (k < i) off += s;
  }
  item->p = v.p + off;
  item->n = s;
  return true;
}

bool wl_cbor_get_str(wl_cbor_t map, const char *key, char *out, size_t out_size) {
  wl_cbor_t v;
  const char *s;
  size_t n;
  if (!wl_cbor_map_get(map, key, &v) || !wl_cbor_text(v, &s, &n) || n >= out_size) return false;
  memcpy(out, s, n);
  out[n] = 0;
  return memchr(out, 0, n) == NULL; /* no embedded NULs */
}

/* ---------------------------------------------------------------- writer */
void wl_cbor_w_init(wl_cbor_w_t *w, uint8_t *buf, size_t cap) {
  w->buf = buf;
  w->cap = cap;
  w->len = 0;
  w->overflow = false;
}

static void put(wl_cbor_w_t *w, const void *d, size_t n) {
  if (w->overflow || n > w->cap - w->len) {
    w->overflow = true;
    return;
  }
  memcpy(w->buf + w->len, d, n);
  w->len += n;
}

static void put_head(wl_cbor_w_t *w, uint8_t major, uint64_t v) {
  uint8_t b[9];
  size_t n;
  if (v < 24) {
    b[0] = (uint8_t)(major << 5 | v);
    n = 1;
  } else if (v <= 0xFF) {
    b[0] = (uint8_t)(major << 5 | 24);
    b[1] = (uint8_t)v;
    n = 2;
  } else if (v <= 0xFFFF) {
    b[0] = (uint8_t)(major << 5 | 25);
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)v;
    n = 3;
  } else if (v <= 0xFFFFFFFFu) {
    b[0] = (uint8_t)(major << 5 | 26);
    for (int i = 0; i < 4; i++) b[1 + i] = (uint8_t)(v >> (24 - 8 * i));
    n = 5;
  } else {
    b[0] = (uint8_t)(major << 5 | 27);
    for (int i = 0; i < 8; i++) b[1 + i] = (uint8_t)(v >> (56 - 8 * i));
    n = 9;
  }
  put(w, b, n);
}

void wl_cbor_w_map(wl_cbor_w_t *w, size_t pairs) { put_head(w, 5, pairs); }
void wl_cbor_w_array(wl_cbor_w_t *w, size_t items) { put_head(w, 4, items); }
void wl_cbor_w_uint(wl_cbor_w_t *w, uint64_t v) { put_head(w, 0, v); }
void wl_cbor_w_text(wl_cbor_w_t *w, const char *s) {
  size_t n = strlen(s);
  put_head(w, 3, n);
  put(w, s, n);
}
void wl_cbor_w_bytes(wl_cbor_w_t *w, const uint8_t *b, size_t n) {
  put_head(w, 2, n);
  put(w, b, n);
}
void wl_cbor_w_bool(wl_cbor_w_t *w, bool v) {
  uint8_t b = v ? 0xf5 : 0xf4;
  put(w, &b, 1);
}
void wl_cbor_w_kv_text(wl_cbor_w_t *w, const char *k, const char *v) {
  wl_cbor_w_text(w, k);
  wl_cbor_w_text(w, v);
}
void wl_cbor_w_kv_uint(wl_cbor_w_t *w, const char *k, uint64_t v) {
  wl_cbor_w_text(w, k);
  wl_cbor_w_uint(w, v);
}
void wl_cbor_w_kv_bool(wl_cbor_w_t *w, const char *k, bool v) {
  wl_cbor_w_text(w, k);
  wl_cbor_w_bool(w, v);
}
void wl_cbor_w_kv_bytes(wl_cbor_w_t *w, const char *k, const uint8_t *b, size_t n) {
  wl_cbor_w_text(w, k);
  wl_cbor_w_bytes(w, b, n);
}
