#include "wallet/entropy.h"

#include <string.h>

#include "hmac.h"
#include "wallet/secure.h"

enum { POOL_TRNG = 0, POOL_SE = 1, POOL_EXTRA = 2 };

static const char DOMAIN[] = "tredder-wallet seed entropy v1";

void wl_entropy_init(wl_entropy_t *e) {
  wl_wipe(e, sizeof(*e));
  for (int i = 0; i < 3; i++) sha512_Init(&e->pool[i]);
  e->initialised = 1;
}

void wl_entropy_wipe(wl_entropy_t *e) { wl_wipe(e, sizeof(*e)); }

static void health_fail(wl_entropy_t *e) {
  wl_entropy_wipe(e);
  e->failed = 1; /* latched: extract() refuses until wl_entropy_init() */
}

/* NIST SP 800-90B 4.4.1 repetition count + 4.4.2 adaptive proportion,
   applied to every raw byte before it is accepted. */
static int health_ok(wl_entropy_t *e, uint8_t s) {
  if (e->rct_count && s == e->rct_last) {
    if (++e->rct_count >= WL_RCT_CUTOFF) return 0;
  } else {
    e->rct_last = s;
    e->rct_count = 1;
  }
  if (e->apt_seen == 0) {
    e->apt_first = s;
    e->apt_count = 1;
    e->apt_seen = 1;
  } else {
    if (s == e->apt_first && ++e->apt_count >= WL_APT_CUTOFF) return 0;
    if (++e->apt_seen >= WL_APT_WINDOW) e->apt_seen = 0;
  }
  return 1;
}

wl_status_t wl_entropy_add_trng(wl_entropy_t *e, const uint8_t *raw, size_t n) {
  if (!e->initialised || e->failed) return WL_ERR_ENTROPY_HEALTH;
  for (size_t i = 0; i < n; i++) {
    if (!health_ok(e, raw[i])) {
      health_fail(e);
      return WL_ERR_ENTROPY_HEALTH;
    }
  }
  sha512_Update(&e->pool[POOL_TRNG], raw, n);
  uint64_t credit = (uint64_t)n * WL_TRNG_BITS_PER_BYTE + e->credited_bits[POOL_TRNG];
  e->credited_bits[POOL_TRNG] = credit > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)credit;
  return WL_OK;
}

wl_status_t wl_entropy_add_se(wl_entropy_t *e, const uint8_t *data, size_t n, uint32_t credited_bits) {
  if (!e->initialised || e->failed) return WL_ERR_ENTROPY_HEALTH;
  if ((uint64_t)credited_bits > 8u * (uint64_t)n) return WL_ERR_ARG;
  /* An SE that returns a constant is broken or hostile: refuse obvious junk. */
  int all_same = n > 1;
  for (size_t i = 1; i < n && all_same; i++) all_same = data[i] == data[0];
  if (all_same) {
    health_fail(e);
    return WL_ERR_ENTROPY_HEALTH;
  }
  sha512_Update(&e->pool[POOL_SE], data, n);
  uint64_t credit = (uint64_t)credited_bits + e->credited_bits[POOL_SE];
  e->credited_bits[POOL_SE] = credit > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)credit;
  return WL_OK;
}

void wl_entropy_add_extra(wl_entropy_t *e, const uint8_t *data, size_t n) {
  if (e->initialised && !e->failed) sha512_Update(&e->pool[POOL_EXTRA], data, n);
}

wl_status_t wl_entropy_extract(wl_entropy_t *e, uint8_t *out, size_t n) {
  if (n == 0 || n > 64) return WL_ERR_ARG;
  if (!e->initialised || e->failed) {
    wl_entropy_wipe(e);
    return WL_ERR_ENTROPY_HEALTH;
  }
  if (e->credited_bits[POOL_TRNG] < WL_ENTROPY_MIN_BITS || e->credited_bits[POOL_SE] < WL_ENTROPY_MIN_BITS) {
    wl_entropy_wipe(e);
    return WL_ERR_ENTROPY_LOW;
  }
  uint8_t h[3][64];
  for (int i = 0; i < 3; i++) sha512_Final(&e->pool[i], h[i]);

  HMAC_SHA512_CTX ctx;
  uint8_t mac[64];
  hmac_sha512_Init(&ctx, h[POOL_TRNG], sizeof(h[POOL_TRNG]));
  hmac_sha512_Update(&ctx, (const uint8_t *)DOMAIN, sizeof(DOMAIN) - 1);
  hmac_sha512_Update(&ctx, h[POOL_SE], sizeof(h[POOL_SE]));
  hmac_sha512_Update(&ctx, h[POOL_EXTRA], sizeof(h[POOL_EXTRA]));
  hmac_sha512_Final(&ctx, mac);
  memcpy(out, mac, n);

  wl_wipe(mac, sizeof(mac));
  wl_wipe(h, sizeof(h));
  wl_wipe(&ctx, sizeof(ctx));
  wl_entropy_wipe(e);
  return WL_OK;
}
