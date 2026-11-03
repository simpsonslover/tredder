/*
 * Behavioural model of the NXP SE050 as the firmware uses it (app/se.h):
 *   - binary object holding a 32-byte secret, readable only in a session
 *     authenticated with the PIN key
 *   - attempt counter (the PIN key's MaxAttempts): spent and persisted BEFORE
 *     comparing, reset on success; at 10 the secret and key are gone for good
 *   - TRNG output
 * Persisted to se050.bin in the state directory, separately from flash.bin,
 * the way the real chip is separate from the MCU's flash.
 * Not modelled: the I2C wire protocol (T=1 over I2C, Platform SCP03).
 */
#include <string.h>

#include "app/se.h"
#include "emu.h"
#include "wallet/secure.h"

#define SE_FILE "se050.bin"

static struct {
  uint8_t magic[4];
  uint8_t provisioned;
  uint8_t counter;
  uint8_t pin_ref[32];
  uint8_t secret[32];
} S;

static void save(void) {
  if (!emu_opts.ephemeral) emu_write_file(SE_FILE, &S, sizeof(S));
}

void se_model_load(void) {
  memset(&S, 0, sizeof(S));
  if (!emu_opts.ephemeral && emu_read_file(SE_FILE, &S, sizeof(S)) && !memcmp(S.magic, "SE05", 4)) return;
  memset(&S, 0, sizeof(S));
  memcpy(S.magic, "SE05", 4);
}

se_status_t se_init(void) { return SE_OK; }

se_status_t se_random(uint8_t *out, size_t n) {
  emu_random(out, n);
  return SE_OK;
}

se_status_t se_pin_provision(const uint8_t pin_ref[32], uint8_t secret[32]) {
  emu_random(S.secret, 32);
  memcpy(S.pin_ref, pin_ref, 32);
  S.counter = 0;
  S.provisioned = 1;
  save();
  memcpy(secret, S.secret, 32);
  return SE_OK;
}

se_status_t se_pin_unlock(const uint8_t pin_ref[32], uint8_t secret[32], uint8_t *tries_left) {
  if (!S.provisioned || S.counter >= SE_PIN_MAX_TRIES) {
    *tries_left = 0;
    return SE_BLOCKED;
  }
  S.counter++;
  save(); /* counted before the comparison: pulling power mid-attempt still costs a try */
  if (wl_ct_equal(pin_ref, S.pin_ref, 32)) {
    S.counter = 0;
    save();
    memcpy(secret, S.secret, 32);
    *tries_left = SE_PIN_MAX_TRIES;
    return SE_OK;
  }
  *tries_left = (uint8_t)(SE_PIN_MAX_TRIES - S.counter);
  if (S.counter >= SE_PIN_MAX_TRIES) {
    wl_wipe(S.secret, sizeof(S.secret));
    wl_wipe(S.pin_ref, sizeof(S.pin_ref));
    S.provisioned = 0;
    save();
    emu_logf("[se050] attempt counter reached %u: secret erased", SE_PIN_MAX_TRIES);
    return SE_BLOCKED;
  }
  return SE_WRONG_PIN;
}

uint8_t se_pin_tries_left(void) { return S.provisioned ? (uint8_t)(SE_PIN_MAX_TRIES - S.counter) : SE_PIN_MAX_TRIES; }
uint8_t se_model_tries_left(void) { return se_pin_tries_left(); }

se_status_t se_wipe(void) {
  wl_wipe(S.secret, sizeof(S.secret));
  wl_wipe(S.pin_ref, sizeof(S.pin_ref));
  S.provisioned = 0;
  S.counter = 0;
  save();
  return SE_OK;
}
