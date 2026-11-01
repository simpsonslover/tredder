#ifndef APP_SE_H
#define APP_SE_H

/*
 * Secure element (Infineon OPTIGA Trust M on I2C0) as the firmware uses it.
 *
 * PIN protection model:
 *  - The SE holds a random 32-byte secret in a data object whose read access
 *    condition is "authorised by the PIN reference AND limit-use counter below
 *    10". The PIN reference is a 32-byte value derived from the PIN on the MCU.
 *  - Every unlock attempt increments the counter BEFORE the comparison, so
 *    cutting power mid-attempt still costs an attempt. A correct PIN resets it.
 *  - At 10 failures the SE blocks and erases the secret. Without it the
 *    encrypted seed in flash can never be decrypted again, even by an
 *    attacker who dumps the flash and bypasses the firmware.
 *
 * platform/rp2350: OPTIGA Trust M host library over I2C (firmware milestone M2).
 * platform/emu:    behavioural model of the same rules (se_model.c).
 */

#include <stddef.h>
#include <stdint.h>

#define SE_PIN_MAX_TRIES 10u

typedef enum {
  SE_OK = 0,
  SE_WRONG_PIN,  /* counter incremented, tries_left updated */
  SE_BLOCKED,    /* counter exhausted: secret erased */
  SE_ERROR,      /* communication / chip error */
} se_status_t;

se_status_t se_init(void);
/* SE true RNG output (conditioned by the SE). */
se_status_t se_random(uint8_t *out, size_t n);
/* New random secret bound to pin_ref; counter reset to 0. Returns the secret. */
se_status_t se_pin_provision(const uint8_t pin_ref[32], uint8_t secret[32]);
/* Counter++, then compare. On SE_OK the counter resets and secret is filled. */
se_status_t se_pin_unlock(const uint8_t pin_ref[32], uint8_t secret[32], uint8_t *tries_left);
uint8_t se_pin_tries_left(void);
/* Erase the secret and PIN reference. */
se_status_t se_wipe(void);

#endif
