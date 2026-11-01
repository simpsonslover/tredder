/*
 * Platform hooks required by trezor-crypto on the RP2350.
 *
 * MILESTONE M1 (bring-up): random_buffer() uses the Pico SDK's pico_rand,
 * which on RP2350 draws on the hardware TRNG. It is only used by library
 * internals (e.g. side-channel blinding); SEEDS DO NOT COME FROM HERE. Seed
 * entropy always goes through core/src/entropy.c (raw TRNG + SE, health-tested).
 * M2 replaces this with an HMAC-DRBG seeded from TRNG + secure element.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pico/rand.h"
#include "pico/stdlib.h"

void random_reseed(const uint32_t value) { (void)value; /* pico_rand reseeds itself from hardware */ }

void random_buffer(uint8_t *buf, size_t len) {
  while (len) {
    uint64_t r = get_rand_64();
    size_t n = len < sizeof(r) ? len : sizeof(r);
    memcpy(buf, &r, n);
    buf += n;
    len -= n;
    r = 0;
  }
}

/* Called when trezor-crypto detects a fault (possible glitch attack).
   Never return. M2: also wipe RAM-resident secrets before halting. */
void tc_fault_handler(const char *msg) { panic("crypto fault: %s", msg); }
