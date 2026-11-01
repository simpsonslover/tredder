#ifndef WALLET_CBOR_H
#define WALLET_CBOR_H

/*
 * Minimal CBOR (RFC 8949) for protocol v1 messages: strict reader over
 * untrusted input + writer for responses. Only definite lengths. The reader
 * never allocates: values are spans into the received buffer.
 *
 * Accepted from the app's encoder (cbor-x): tag 64 around byte strings
 * (Uint8Array), float64 for integral numbers, `undefined` for absent fields.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  const uint8_t *p;
  size_t n; /* exact length of this one encoded item */
} wl_cbor_t;

/* Whole buffer must be exactly one well-formed item (depth <= 8). */
bool wl_cbor_parse(const uint8_t *buf, size_t len, wl_cbor_t *root);

bool wl_cbor_map_get(wl_cbor_t map, const char *key, wl_cbor_t *val); /* false if missing/undefined/null */
bool wl_cbor_uint(wl_cbor_t v, uint64_t *out);                        /* uint or integral float */
bool wl_cbor_text(wl_cbor_t v, const char **s, size_t *n);
bool wl_cbor_bytes(wl_cbor_t v, const uint8_t **b, size_t *n);
bool wl_cbor_array(wl_cbor_t v, size_t *count);
bool wl_cbor_array_at(wl_cbor_t v, size_t i, wl_cbor_t *item);
bool wl_cbor_is_map(wl_cbor_t v);
/* Text value equals s. */
bool wl_cbor_text_eq(wl_cbor_t v, const char *s);
/* Copy a text value into out (NUL-terminated); false if missing or too long. */
bool wl_cbor_get_str(wl_cbor_t map, const char *key, char *out, size_t out_size);

/* ---------------------------------------------------------------- writer */
typedef struct {
  uint8_t *buf;
  size_t cap, len;
  bool overflow;
} wl_cbor_w_t;

void wl_cbor_w_init(wl_cbor_w_t *w, uint8_t *buf, size_t cap);
void wl_cbor_w_map(wl_cbor_w_t *w, size_t pairs);
void wl_cbor_w_array(wl_cbor_w_t *w, size_t items);
void wl_cbor_w_uint(wl_cbor_w_t *w, uint64_t v);
void wl_cbor_w_text(wl_cbor_w_t *w, const char *s);
void wl_cbor_w_bytes(wl_cbor_w_t *w, const uint8_t *b, size_t n);
void wl_cbor_w_bool(wl_cbor_w_t *w, bool v);
/* Key + value helpers for maps. */
void wl_cbor_w_kv_text(wl_cbor_w_t *w, const char *k, const char *v);
void wl_cbor_w_kv_uint(wl_cbor_w_t *w, const char *k, uint64_t v);
void wl_cbor_w_kv_bool(wl_cbor_w_t *w, const char *k, bool v);
void wl_cbor_w_kv_bytes(wl_cbor_w_t *w, const char *k, const uint8_t *b, size_t n);

#endif
