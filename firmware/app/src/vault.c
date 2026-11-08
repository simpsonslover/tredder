#include "app/vault.h"

#include <string.h>

#include "app/hal.h"
#include "app/se.h"
#include "chacha20poly1305/rfc7539.h"
#include "hmac.h"
#include "pbkdf2.h"
#include "rand.h"
#include "wallet/mnemonic.h"
#include "wallet/secure.h"

#define PIN_ITERS 10000u
#define PT_LEN 100u

/* Flash record. Serialised field by field (no struct padding on flash). */
#define REC_MAGIC "TRV1"
#define OFF_MAGIC 0
#define OFF_SEQ 4
#define OFF_FLAGS 8
#define OFF_SALT 9
#define OFF_NONCE 25
#define OFF_CT 37
#define OFF_TAG (OFF_CT + PT_LEN)
#define OFF_CRC (OFF_TAG + 16)
#define REC_LEN (OFF_CRC + 4)
#define AAD_LEN OFF_NONCE /* magic, seq, flags, salt are authenticated */

#define FLAG_SWEEP 0x01

static struct {
  bool present;
  uint8_t rec[REC_LEN];
  uint32_t seq;
  int sector;
  /* unlocked state */
  bool unlocked;
  uint8_t key[32];
  uint8_t pin_key[32];
  struct {
    uint8_t words, sweep_words, blind;
    uint16_t idx[24], sweep_idx[24];
  } pt;
  uint8_t seed[64], sweep_seed[64];
} V;

static uint32_t crc32(const uint8_t *d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = c >> 1 ^ (0xEDB88320u & (uint32_t)-(int32_t)(c & 1));
  }
  return ~c;
}
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void wr32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

void vault_init(void) {
  memset(&V, 0, sizeof(V));
  uint8_t r[REC_LEN];
  int best = -1;
  uint32_t best_seq = 0;
  for (int s = 0; s < (int)HAL_STORAGE_SECTORS; s++) {
    if (!hal_flash_read((uint32_t)s * HAL_FLASH_SECTOR, r, REC_LEN)) continue;
    if (memcmp(r, REC_MAGIC, 4) || crc32(r, OFF_CRC) != rd32(r + OFF_CRC)) continue;
    uint32_t seq = rd32(r + OFF_SEQ);
    if (best < 0 || seq > best_seq) {
      best = s, best_seq = seq;
      memcpy(V.rec, r, REC_LEN);
    }
  }
  if (best >= 0) {
    V.present = true, V.seq = best_seq, V.sector = best;
    /* A power cut between "write new" and "erase old" leaves a stale record: erase it now. */
    for (int s = 0; s < (int)HAL_STORAGE_SECTORS; s++) {
      if (s != best && hal_flash_read((uint32_t)s * HAL_FLASH_SECTOR, r, 4) && !memcmp(r, REC_MAGIC, 4))
        hal_flash_erase((uint32_t)s);
    }
  }
  wl_wipe(r, sizeof(r));
}

bool vault_initialized(void) { return V.present; }
bool vault_unlocked(void) { return V.unlocked; }
bool vault_has_sweep(void) { return V.present && (V.rec[OFF_FLAGS] & FLAG_SWEEP); }
uint8_t vault_tries_left(void) { return se_pin_tries_left(); }

static void derive_pin(const char *pin, const uint8_t salt[16], uint8_t pin_key[32], uint8_t pin_ref[32]) {
  pbkdf2_hmac_sha256((const uint8_t *)pin, strlen(pin), salt, 16, PIN_ITERS, pin_key, 32);
  static const char REF[] = "tredder se pin ref";
  hmac_sha256(pin_key, 32, (const uint8_t *)REF, sizeof(REF) - 1, pin_ref);
}

static void derive_key(const uint8_t secret[32], const uint8_t pin_key[32], uint8_t key[32]) {
  uint8_t msg[32 + 17];
  memcpy(msg, pin_key, 32);
  memcpy(msg + 32, "tredder vault key", 17);
  hmac_sha256(secret, 32, msg, sizeof(msg), key);
  wl_wipe(msg, sizeof(msg));
}

static void pt_serialise(uint8_t out[PT_LEN]) {
  memset(out, 0, PT_LEN);
  out[0] = 1; /* plaintext version */
  out[1] = V.pt.words;
  for (int i = 0; i < 24; i++) out[2 + 2 * i] = (uint8_t)V.pt.idx[i], out[3 + 2 * i] = (uint8_t)(V.pt.idx[i] >> 8);
  out[50] = V.pt.sweep_words;
  for (int i = 0; i < 24; i++)
    out[51 + 2 * i] = (uint8_t)V.pt.sweep_idx[i], out[52 + 2 * i] = (uint8_t)(V.pt.sweep_idx[i] >> 8);
  out[99] = V.pt.blind;
}

static bool pt_parse(const uint8_t in[PT_LEN]) {
  if (in[0] != 1) return false;
  V.pt.words = in[1];
  for (int i = 0; i < 24; i++) V.pt.idx[i] = (uint16_t)(in[2 + 2 * i] | in[3 + 2 * i] << 8);
  V.pt.sweep_words = in[50];
  for (int i = 0; i < 24; i++) V.pt.sweep_idx[i] = (uint16_t)(in[51 + 2 * i] | in[52 + 2 * i] << 8);
  V.pt.blind = in[99];
  return wl_mnemonic_check_indices(V.pt.idx, V.pt.words) == WL_OK &&
         (V.pt.sweep_words == 0 || wl_mnemonic_check_indices(V.pt.sweep_idx, V.pt.sweep_words) == WL_OK);
}

/* Encrypt the current plaintext under V.key into a new record and commit it. */
static vault_result_t commit(const uint8_t salt[16]) {
  uint8_t r[REC_LEN], pt[PT_LEN];
  memset(r, 0xFF, sizeof(r));
  memcpy(r + OFF_MAGIC, REC_MAGIC, 4);
  wr32(r + OFF_SEQ, V.seq + 1);
  r[OFF_FLAGS] = V.pt.sweep_words ? FLAG_SWEEP : 0;
  memcpy(r + OFF_SALT, salt, 16);
  random_buffer(r + OFF_NONCE, 12); /* fresh nonce per write: never reused under one key */
  pt_serialise(pt);
  chacha20poly1305_ctx c;
  rfc7539_init(&c, V.key, r + OFF_NONCE);
  rfc7539_auth(&c, r, AAD_LEN);
  chacha20poly1305_encrypt(&c, pt, r + OFF_CT, PT_LEN);
  rfc7539_finish(&c, AAD_LEN, PT_LEN, r + OFF_TAG);
  wr32(r + OFF_CRC, crc32(r, OFF_CRC));
  wl_wipe(pt, sizeof(pt));
  wl_wipe(&c, sizeof(c));

  int next = V.present ? (V.sector + 1) % (int)HAL_STORAGE_SECTORS : 0;
  uint8_t check[REC_LEN];
  bool ok = hal_flash_erase((uint32_t)next) && hal_flash_program((uint32_t)next * HAL_FLASH_SECTOR, r, REC_LEN) &&
            hal_flash_read((uint32_t)next * HAL_FLASH_SECTOR, check, REC_LEN) && !memcmp(check, r, REC_LEN);
  if (!ok) return VAULT_ERROR;
  /* Only now erase the previous record, so old secrets (e.g. a forgotten sweep seed) are gone. */
  if (V.present && V.sector != next) hal_flash_erase((uint32_t)V.sector);
  memcpy(V.rec, r, REC_LEN);
  V.present = true, V.sector = next, V.seq++;
  return VAULT_OK;
}

static bool derive_seeds(void) {
  if (wl_mnemonic_to_seed(V.pt.idx, V.pt.words, "", V.seed) != WL_OK) return false;
  if (V.pt.sweep_words && wl_mnemonic_to_seed(V.pt.sweep_idx, V.pt.sweep_words, "", V.sweep_seed) != WL_OK) return false;
  return true;
}

vault_result_t vault_create(const uint16_t *idx, size_t words, const char *pin) {
  size_t pl = strlen(pin);
  if (pl < VAULT_PIN_MIN || pl > VAULT_PIN_MAX || wl_mnemonic_check_indices(idx, words) != WL_OK) return VAULT_ERROR;
  vault_wipe(); /* a new wallet always starts from a clean device */
  uint8_t salt[16], pin_ref[32], secret[32];
  random_buffer(salt, sizeof(salt));
  derive_pin(pin, salt, V.pin_key, pin_ref);
  vault_result_t res = VAULT_ERROR;
  if (se_pin_provision(pin_ref, secret) == SE_OK) {
    derive_key(secret, V.pin_key, V.key);
    V.pt.words = (uint8_t)words;
    memcpy(V.pt.idx, idx, words * sizeof(uint16_t));
    res = commit(salt);
    if (res == VAULT_OK && derive_seeds()) V.unlocked = true;
    else res = VAULT_ERROR;
  }
  wl_wipe(pin_ref, sizeof(pin_ref));
  wl_wipe(secret, sizeof(secret));
  if (res != VAULT_OK) vault_lock();
  return res;
}

static vault_result_t open_with_pin(const char *pin, bool keep) {
  if (!V.present) return VAULT_ERROR;
  uint8_t pin_key[32], pin_ref[32], secret[32], key[32], pt[PT_LEN], tag[16];
  uint8_t tries = 0;
  vault_result_t res;
  derive_pin(pin, V.rec + OFF_SALT, pin_key, pin_ref);
  se_status_t st = se_pin_unlock(pin_ref, secret, &tries);
  if (st == SE_BLOCKED) {
    vault_wipe();
    res = VAULT_WIPED;
  } else if (st == SE_WRONG_PIN) {
    res = tries ? VAULT_WRONG_PIN : (vault_wipe(), VAULT_WIPED);
  } else if (st != SE_OK) {
    res = VAULT_ERROR;
  } else {
    derive_key(secret, pin_key, key);
    chacha20poly1305_ctx c;
    rfc7539_init(&c, key, V.rec + OFF_NONCE);
    rfc7539_auth(&c, V.rec, AAD_LEN);
    chacha20poly1305_decrypt(&c, V.rec + OFF_CT, pt, PT_LEN);
    rfc7539_finish(&c, AAD_LEN, PT_LEN, tag);
    wl_wipe(&c, sizeof(c));
    if (!wl_ct_equal(tag, V.rec + OFF_TAG, 16)) {
      res = VAULT_ERROR; /* flash tampered or corrupted */
    } else if (keep) {
      memcpy(V.key, key, 32);
      memcpy(V.pin_key, pin_key, 32);
      res = pt_parse(pt) && derive_seeds() ? VAULT_OK : VAULT_ERROR;
      if (res == VAULT_OK) V.unlocked = true;
      else vault_lock();
    } else {
      res = VAULT_OK;
    }
  }
  wl_wipe(pin_key, sizeof(pin_key));
  wl_wipe(pin_ref, sizeof(pin_ref));
  wl_wipe(secret, sizeof(secret));
  wl_wipe(key, sizeof(key));
  wl_wipe(pt, sizeof(pt));
  return res;
}

vault_result_t vault_unlock(const char *pin) { return open_with_pin(pin, true); }

vault_result_t vault_check_pin(const char *pin) {
  if (!V.unlocked) return VAULT_ERROR;
  return open_with_pin(pin, false);
}

void vault_lock(void) {
  wl_wipe(V.key, sizeof(V.key));
  wl_wipe(V.pin_key, sizeof(V.pin_key));
  wl_wipe(&V.pt, sizeof(V.pt));
  wl_wipe(V.seed, sizeof(V.seed));
  wl_wipe(V.sweep_seed, sizeof(V.sweep_seed));
  V.unlocked = false;
}

void vault_wipe(void) {
  vault_lock();
  se_wipe();
  for (uint32_t s = 0; s < HAL_STORAGE_SECTORS; s++) hal_flash_erase(s);
  wl_wipe(V.rec, sizeof(V.rec));
  V.present = false, V.seq = 0, V.sector = 0;
}

const uint8_t *vault_seed(void) { return V.unlocked ? V.seed : NULL; }
const uint8_t *vault_sweep_seed(void) { return V.unlocked && V.pt.sweep_words ? V.sweep_seed : NULL; }
bool vault_blind_signing(void) { return V.unlocked && V.pt.blind; }

static vault_result_t recommit(void) {
  uint8_t salt[16];
  memcpy(salt, V.rec + OFF_SALT, 16);
  vault_result_t r = commit(salt);
  if (r == VAULT_OK && !derive_seeds()) r = VAULT_ERROR;
  return r;
}

vault_result_t vault_set_blind_signing(bool on) {
  if (!V.unlocked) return VAULT_ERROR;
  V.pt.blind = on ? 1 : 0;
  return recommit();
}

vault_result_t vault_set_sweep(const uint16_t *idx, size_t words) {
  if (!V.unlocked || (words && wl_mnemonic_check_indices(idx, words) != WL_OK)) return VAULT_ERROR;
  memset(V.pt.sweep_idx, 0, sizeof(V.pt.sweep_idx));
  if (words) memcpy(V.pt.sweep_idx, idx, words * sizeof(uint16_t));
  V.pt.sweep_words = (uint8_t)words;
  if (!words) wl_wipe(V.sweep_seed, sizeof(V.sweep_seed));
  return recommit();
}

vault_result_t vault_replace_active(const uint16_t *idx, size_t words) {
  if (!V.unlocked || wl_mnemonic_check_indices(idx, words) != WL_OK) return VAULT_ERROR;
  memset(V.pt.idx, 0, sizeof(V.pt.idx));
  memcpy(V.pt.idx, idx, words * sizeof(uint16_t));
  V.pt.words = (uint8_t)words;
  memset(V.pt.sweep_idx, 0, sizeof(V.pt.sweep_idx));
  V.pt.sweep_words = 0;
  return recommit();
}

vault_result_t vault_export_backup(uint16_t idx[24], uint8_t *words, bool *blind) {
  memset(idx, 0, 24 * sizeof(uint16_t));
  *words = 0;
  *blind = false;
  if (!V.unlocked || !V.pt.words) return VAULT_ERROR;
  memcpy(idx, V.pt.idx, V.pt.words * sizeof(uint16_t));
  *words = V.pt.words;
  *blind = V.pt.blind != 0;
  return VAULT_OK;
}
