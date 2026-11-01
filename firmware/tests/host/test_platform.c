/*
 * TEST ONLY: host stand-ins for the platform hooks trezor-crypto needs.
 *   random_buffer / random_reseed: deterministic (device: HMAC-DRBG seeded from
 *     the RP2350 TRNG + secure element).
 *   tc_fault_handler: abort (device: wipe secrets and reset).
 * This file must never be linked into firmware: the device build does not
 * include tests/.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Called by trezor-crypto when a fault-injection check trips (e.g. consteq
   loop completion). Never return: continuing after a detected glitch is unsafe. */
void tc_fault_handler(const char *msg) {
  fprintf(stderr, "FAULT DETECTED: %s\n", msg);
  abort();
}

static uint64_t s = 0x9E3779B97F4A7C15ull;

void random_reseed(const uint32_t value) { s = value ? value : 1; }

void random_buffer(uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    buf[i] = (uint8_t)(s >> 24);
  }
}
