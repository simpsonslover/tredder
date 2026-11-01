/*
 * random_buffer() for the libraries and the UI (PIN pad shuffles, nonces,
 * signing blinding): HMAC-DRBG (SP 800-90A) seeded from the RP2350 TRNG and
 * the secure element through the health-tested entropy pool, reseeded
 * regularly from the TRNG. NEW SEEDS DO NOT COME FROM HERE: wallet creation
 * draws directly from the entropy pool (flows in ui.c).
 */
#include <stddef.h>
#include <stdint.h>

#include "app/hal.h"
#include "app/rng.h"
#include "app/se.h"
#include "hmac_drbg.h"
#include "wallet/entropy.h"
#include "wallet/secure.h"

static HMAC_DRBG_CTX drbg;
static int seeded;
static uint32_t since_reseed;

bool rng_collect(uint8_t *out, size_t n) {
  wl_entropy_t e;
  uint8_t raw[512], se[32];
  wl_entropy_init(&e);
  hal_trng_raw(raw, sizeof(raw)); /* 512 bytes credited at 1 bit/byte */
  bool ok = wl_entropy_add_trng(&e, raw, sizeof(raw)) == WL_OK && se_random(se, sizeof(se)) == SE_OK &&
            wl_entropy_add_se(&e, se, sizeof(se), 256) == WL_OK && wl_entropy_extract(&e, out, n) == WL_OK;
  wl_entropy_wipe(&e);
  wl_wipe(raw, sizeof(raw));
  wl_wipe(se, sizeof(se));
  return ok;
}

static void seed_now(void) {
  uint8_t s[64];
  uint32_t t = hal_millis();
  if (!rng_collect(s, sizeof(s))) rng_fatal("entropy source failed its health test");
  hmac_drbg_init(&drbg, s, sizeof(s), (const uint8_t *)&t, sizeof(t));
  wl_wipe(s, sizeof(s));
  seeded = 1;
  since_reseed = 0;
}

void random_reseed(const uint32_t value) { (void)value; }

void random_buffer(uint8_t *buf, size_t len) {
  if (!seeded || ++since_reseed > 4096) seed_now();
  while (len) {
    size_t n = len > 64 ? 64 : len;
    hmac_drbg_generate(&drbg, buf, n);
    buf += n;
    len -= n;
  }
}
