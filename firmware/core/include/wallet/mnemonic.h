#ifndef WALLET_MNEMONIC_H
#define WALLET_MNEMONIC_H

#include <stddef.h>
#include <stdint.h>

#include "wallet/status.h"

/*
 * BIP39 mnemonics (English wordlist).
 *
 * On the device the user never types free text: each word is picked letter
 * by letter with autocomplete, so internally a mnemonic is an array of word
 * indices (0..2047). That avoids string parsing of secret input, and the
 * string form is only built in a caller-owned buffer when needed for
 * seed derivation, then wiped.
 */

#define WL_MNEMONIC_MAX_WORDS 24
#define WL_MNEMONIC_MAX_LEN (WL_MNEMONIC_MAX_WORDS * 9)  /* 8 letters + space, incl. NUL */
#define WL_PASSPHRASE_MAX_LEN 100

/* Number of words for 128/160/192/224/256-bit entropy. */
int wl_mnemonic_valid_word_count(size_t words);

/* Entropy (16..32 bytes, multiple of 4) -> word indices. */
wl_status_t wl_mnemonic_from_entropy(const uint8_t *entropy, size_t len, uint16_t *idx, size_t *words);

/* Validate indices: word count + checksum. */
wl_status_t wl_mnemonic_check_indices(const uint16_t *idx, size_t words);

/* Validate a space-separated string. On WL_ERR_UNKNOWN_WORD *bad_word is the
   1-based position of the first unknown word (else 0). On success fills idx/words
   (idx may be NULL). Used for vectors and for import from microSD backups. */
wl_status_t wl_mnemonic_parse(const char *mnemonic, uint16_t *idx, size_t *words, size_t *bad_word);

/* Indices -> "word word word" (out >= WL_MNEMONIC_MAX_LEN). */
wl_status_t wl_mnemonic_to_string(const uint16_t *idx, size_t words, char *out, size_t out_size);

/* BIP39 seed = PBKDF2-HMAC-SHA512(mnemonic, "mnemonic" || passphrase, 2048).
   Passphrases are restricted to printable ASCII (0x20..0x7E) because that is
   what the button UI can enter; for ASCII, BIP39's NFKD normalisation is the
   identity, so results match every other BIP39 wallet. */
wl_status_t wl_mnemonic_to_seed(const uint16_t *idx, size_t words, const char *passphrase, uint8_t seed[64]);

/* Wordlist access for the on-device word picker. */
const char *wl_word(uint16_t index);       /* NULL if index >= 2048 */
int wl_word_index(const char *word);       /* constant-time lookup, -1 if unknown */

/* Autocomplete: number of words starting with `prefix` (always scans the whole
   list, no early exit). Up to `max` matching indices are written to out. */
size_t wl_word_complete(const char *prefix, uint16_t *out, size_t max);

/* Bitmask of letters that can follow `prefix` (bit 0 = 'a'): the picker only
   offers letters that lead to a real word. */
uint32_t wl_word_next_letters(const char *prefix);

#endif
