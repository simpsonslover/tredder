#ifndef APP_SE_H
#define APP_SE_H

/*
 * Secure element (NXP SE050C2 on I2C0, address 0x48) as the firmware uses it.
 *
 * PIN protection model:
 *  - The SE holds a random 32-byte secret in a binary-file object whose policy
 *    allows reading only in a session authenticated with the PIN key. The PIN
 *    key is an AES authentication object (derived on the MCU from the 32-byte
 *    PIN reference) created with MaxAttempts = 10, so the PIN itself never
 *    crosses the bus: the session is opened with an SCP03-style challenge.
 *  - The attempt counter lives in the SE and is spent BEFORE the comparison
 *    result is known, so cutting power mid-attempt still costs an attempt.
 *    A correct PIN resets it.
 *  - At 10 failures the authentication object is unusable for good, so the
 *    secret can never be read again; nothing is allowed to delete or rewrite
 *    either object except a full wipe. Without the secret the encrypted seed in
 *    flash can never be decrypted, even by an attacker who dumps the flash and
 *    bypasses the firmware.
 *  - The whole I2C link runs under Platform SCP03 (encrypted + MACed APDUs)
 *    with keys rotated away from NXP's published defaults at provisioning; the
 *    new keys live in RP2350 OTP.
 *  >>> To verify against AN12413 (SE050 APDU spec) when writing the driver:
 *      that a failed AES-key session counts before the result is returned,
 *      and the exact policy bits. <<<
 *
 * platform/rp2350: NXP Plug & Trust middleware (T=1 over I2C) (firmware milestone M2).
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
