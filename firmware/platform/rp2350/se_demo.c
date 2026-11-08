/*
 * DEMO STAND-IN for the NXP SE050 (TREDDER_DEMO_NO_SE builds only): the same rules as
 * app/se.h (secret readable only with the right PIN reference, attempt counter spent
 * before the comparison, erased at 10 failures), kept in RAM, so it is lost at every
 * reset or power cycle, together with the RAM "flash" in hal_board.c.
 *
 * NOT SECURE, and it does not pretend to be: anyone with a debugger can read this RAM,
 * and se_random() is the RP2350's own TRNG (via pico_rand) instead of an independent chip.
 * It exists so the real firmware's screens and flows can be tried on the breadboard.
 */
#include <string.h>

#include "app/se.h"
#include "pico/rand.h"
#include "wallet/secure.h"

#ifndef TREDDER_DEMO_NO_SE
#error "se_demo.c is the no-security-chip demo stand-in; never link it into a real build"
#endif

static struct {
  bool provisioned;
  uint8_t counter;
  uint8_t pin_ref[32];
  uint8_t secret[32];
} S;

se_status_t se_init(void) { return SE_OK; }

se_status_t se_random(uint8_t *out, size_t n) {
  while (n) {
    uint64_t r = get_rand_64();
    size_t k = n < 8 ? n : 8;
    memcpy(out, &r, k);
    out += k, n -= k;
  }
  return SE_OK;
}

se_status_t se_pin_provision(const uint8_t pin_ref[32], uint8_t secret[32]) {
  se_random(S.secret, 32);
  memcpy(S.pin_ref, pin_ref, 32);
  S.counter = 0;
  S.provisioned = true;
  memcpy(secret, S.secret, 32);
  return SE_OK;
}

se_status_t se_pin_unlock(const uint8_t pin_ref[32], uint8_t secret[32], uint8_t *tries_left) {
  if (!S.provisioned || S.counter >= SE_PIN_MAX_TRIES) {
    *tries_left = 0;
    return SE_BLOCKED;
  }
  S.counter++;
  if (wl_ct_equal(pin_ref, S.pin_ref, 32)) {
    S.counter = 0;
    memcpy(secret, S.secret, 32);
    *tries_left = SE_PIN_MAX_TRIES;
    return SE_OK;
  }
  *tries_left = (uint8_t)(SE_PIN_MAX_TRIES - S.counter);
  if (S.counter >= SE_PIN_MAX_TRIES) {
    se_wipe();
    S.counter = SE_PIN_MAX_TRIES;
    return SE_BLOCKED;
  }
  return SE_WRONG_PIN;
}

uint8_t se_pin_tries_left(void) { return S.provisioned ? (uint8_t)(SE_PIN_MAX_TRIES - S.counter) : SE_PIN_MAX_TRIES; }

se_status_t se_wipe(void) {
  wl_wipe(S.secret, sizeof(S.secret));
  wl_wipe(S.pin_ref, sizeof(S.pin_ref));
  S.provisioned = false;
  S.counter = 0;
  return SE_OK;
}
