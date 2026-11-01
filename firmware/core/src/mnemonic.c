#include "wallet/mnemonic.h"

#include <string.h>

#include "bip39.h"
#include "sha2.h"
#include "wallet/secure.h"

#if USE_BIP39_CACHE
#error "USE_BIP39_CACHE must be 0: the cache keeps mnemonic->seed pairs in RAM"
#endif

int wl_mnemonic_valid_word_count(size_t words) {
  return words == 12 || words == 15 || words == 18 || words == 21 || words == 24;
}

const char *wl_word(uint16_t index) {
  return index < BIP39_WORD_COUNT ? BIP39_WORDLIST_ENGLISH[index] : NULL;
}

int wl_word_index(const char *word) {
  /* mnemonic_find_word needs a >= 9 byte buffer; longer input can't be a word. */
  char buf[16] = {0};
  size_t n = strlen(word);
  if (n == 0 || n > 8) return -1;
  memcpy(buf, word, n);
  found_word f = mnemonic_find_word(buf);
  wl_wipe(buf, sizeof(buf));
  return f.index;
}

/* Parse space-separated words into indices without keeping copies of words. */
wl_status_t wl_mnemonic_parse(const char *m, uint16_t *idx, size_t *words, size_t *bad_word) {
  size_t count = 0, first_bad = 0;
  char word[16];
  uint16_t local[WL_MNEMONIC_MAX_WORDS];
  wl_status_t st = WL_OK;
  if (bad_word) *bad_word = 0;

  const char *p = m;
  while (*p) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *start = p;
    while (*p && *p != ' ') p++;
    size_t len = (size_t)(p - start);
    count++;
    int i = -1;
    if (len <= 8) {
      memset(word, 0, sizeof(word));
      memcpy(word, start, len);
      i = wl_word_index(word);
    }
    if (i < 0) {
      if (!first_bad) first_bad = count;
    } else if (count <= WL_MNEMONIC_MAX_WORDS) {
      local[count - 1] = (uint16_t)i;
    }
  }
  wl_wipe(word, sizeof(word));

  if (first_bad) {
    if (bad_word) *bad_word = first_bad;
    st = WL_ERR_UNKNOWN_WORD;
  } else if (!wl_mnemonic_valid_word_count(count)) {
    st = WL_ERR_WORD_COUNT;
  } else {
    st = wl_mnemonic_check_indices(local, count);
  }
  if (st == WL_OK) {
    if (idx) memcpy(idx, local, count * sizeof(uint16_t));
    if (words) *words = count;
  }
  wl_wipe(local, sizeof(local));
  return st;
}

wl_status_t wl_mnemonic_to_string(const uint16_t *idx, size_t words, char *out, size_t out_size) {
  if (!wl_mnemonic_valid_word_count(words)) return WL_ERR_WORD_COUNT;
  size_t pos = 0;
  for (size_t i = 0; i < words; i++) {
    const char *w = wl_word(idx[i]);
    if (!w) {
      wl_wipe(out, out_size);
      return WL_ERR_UNKNOWN_WORD;
    }
    size_t n = strlen(w);
    if (pos + n + 2 > out_size) {
      wl_wipe(out, out_size);
      return WL_ERR_BUFFER;
    }
    if (i) out[pos++] = ' ';
    memcpy(out + pos, w, n);
    pos += n;
  }
  out[pos] = 0;
  return WL_OK;
}

/*
 * BIP39 checksum for every valid length. trezor-crypto's mnemonic_check() only
 * accepts 12/18/24 words (what Trezor devices use), but imports must also work
 * for 15 and 21. mnemonic_to_bits() handles all lengths; the rule is the one
 * from BIP39: ENT = words*32/3 bits, CS = words/3 bits = first CS bits of
 * SHA256(ENT).
 */
static wl_status_t checksum(const char *s, size_t words) {
  uint8_t bits[32 + 1] = {0}, h[32];
  wl_status_t st = WL_ERR_CHECKSUM;
  if (mnemonic_to_bits(s, bits) == words * 11) {
    size_t ent_bytes = words * 4 / 3;              /* 16 20 24 28 32 */
    unsigned cs_bits = (unsigned)(words / 3);      /*  4  5  6  7  8 */
    uint8_t mask = (uint8_t)(0xFFu << (8 - cs_bits));
    sha256_Raw(bits, ent_bytes, h);
    if (((h[0] ^ bits[ent_bytes]) & mask) == 0) st = WL_OK;
  }
  wl_wipe(bits, sizeof(bits));
  wl_wipe(h, sizeof(h));
  return st;
}

wl_status_t wl_mnemonic_check_indices(const uint16_t *idx, size_t words) {
  char s[WL_MNEMONIC_MAX_LEN];
  wl_status_t st = wl_mnemonic_to_string(idx, words, s, sizeof(s));
  if (st == WL_OK) st = checksum(s, words);
  wl_wipe(s, sizeof(s));
  return st;
}

wl_status_t wl_mnemonic_from_entropy(const uint8_t *entropy, size_t len, uint16_t *idx, size_t *words) {
  if (len < 16 || len > 32 || len % 4) return WL_ERR_ARG;
  const char *m = mnemonic_from_data(entropy, len); /* points at a static buffer inside bip39.c */
  wl_status_t st = m ? wl_mnemonic_parse(m, idx, words, NULL) : WL_ERR_CRYPTO;
  mnemonic_clear(); /* wipe that static buffer */
  return st;
}

wl_status_t wl_mnemonic_to_seed(const uint16_t *idx, size_t words, const char *passphrase, uint8_t seed[64]) {
  if (!passphrase) passphrase = "";
  size_t plen = strlen(passphrase);
  if (plen > WL_PASSPHRASE_MAX_LEN) return WL_ERR_PASSPHRASE;
  for (size_t i = 0; i < plen; i++) {
    if ((unsigned char)passphrase[i] < 0x20 || (unsigned char)passphrase[i] > 0x7E) return WL_ERR_PASSPHRASE;
  }
  char s[WL_MNEMONIC_MAX_LEN];
  wl_status_t st = wl_mnemonic_to_string(idx, words, s, sizeof(s));
  if (st == WL_OK) st = checksum(s, words);
  if (st == WL_OK) mnemonic_to_seed(s, passphrase, seed, NULL);
  wl_wipe(s, sizeof(s));
  return st;
}

size_t wl_word_complete(const char *prefix, uint16_t *out, size_t max) {
  size_t plen = strlen(prefix), count = 0;
  for (uint16_t i = 0; i < BIP39_WORD_COUNT; i++) { /* full scan: no early exit */
    if (strncmp(BIP39_WORDLIST_ENGLISH[i], prefix, plen) == 0) {
      if (count < max && out) out[count] = i;
      count++;
    }
  }
  return count;
}

uint32_t wl_word_next_letters(const char *prefix) {
  size_t plen = strlen(prefix);
  uint32_t mask = 0;
  for (int i = 0; i < BIP39_WORD_COUNT; i++) {
    const char *w = BIP39_WORDLIST_ENGLISH[i];
    if (strncmp(w, prefix, plen) == 0 && w[plen] >= 'a' && w[plen] <= 'z') mask |= 1u << (w[plen] - 'a');
  }
  return mask;
}
