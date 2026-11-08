#ifndef APP_VAULT_H
#define APP_VAULT_H

/*
 * Encrypted seed storage + PIN.
 *
 *   pin_key = PBKDF2-HMAC-SHA256(PIN, salt, 10000)       (salt: random, in flash)
 *   pin_ref = HMAC-SHA256(pin_key, "tredder se pin ref")  -> secure element
 *   secret  = released by the SE only for the right pin_ref, counter < 10
 *   K       = HMAC-SHA256(secret, pin_key || "tredder vault key")
 *   flash   = ChaCha20-Poly1305_K(word indices of the seed(s) + settings)
 *
 * The flash alone is useless without the SE secret; the SE alone is useless
 * without the PIN; the SE counter limits PIN guesses to 10 even if the
 * firmware is bypassed. While unlocked the derived BIP39 seeds live in RAM;
 * locking wipes them.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VAULT_PIN_MIN 4
#define VAULT_PIN_MAX 8

typedef enum {
  VAULT_OK = 0,
  VAULT_WRONG_PIN,
  VAULT_WIPED,     /* 10th wrong PIN: secure element erased its secret, flash erased */
  VAULT_ERROR,
} vault_result_t;

void vault_init(void);                 /* load header from flash at boot */
bool vault_initialized(void);
bool vault_unlocked(void);
bool vault_has_sweep(void);
uint8_t vault_tries_left(void);

vault_result_t vault_create(const uint16_t *idx, size_t words, const char *pin);
vault_result_t vault_unlock(const char *pin);
/* Re-check the PIN while unlocked (approvals). Costs an SE attempt if wrong. */
vault_result_t vault_check_pin(const char *pin);
void vault_lock(void);
void vault_wipe(void);

/* Only while unlocked: */
const uint8_t *vault_seed(void);       /* 64-byte BIP39 seed of the active wallet */
const uint8_t *vault_sweep_seed(void); /* NULL if none */
bool vault_blind_signing(void);
vault_result_t vault_set_blind_signing(bool on);
vault_result_t vault_set_sweep(const uint16_t *idx, size_t words); /* words = 0 erases it */
vault_result_t vault_replace_active(const uint16_t *idx, size_t words);

/* Only while unlocked, and only for the encrypted microSD backup (app/fs/sdbackup.c): the
   active wallet's word indices + settings. Never called by the protocol code; the caller wipes
   idx after use. */
vault_result_t vault_export_backup(uint16_t idx[24], uint8_t *words, bool *blind);

#endif
