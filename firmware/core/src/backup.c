/* Encrypted wallet backup file, format v1. See wallet/backup.h. */
#include "wallet/backup.h"

#include <string.h>

#include "chacha20poly1305/rfc7539.h"
#include "monocypher.h"
#include "sha2.h"
#include "wallet/mnemonic.h"
#include "wallet/secure.h"

#define MAGIC "TREDDERB"
#define VERSION 1u
#define FLAG_DEMO 0x01u
#define KDF_ARGON2ID 2u
#define HDR_LEN 52u
#define PT_LEN 64u
#define OFF_CT HDR_LEN
#define OFF_TAG (OFF_CT + PT_LEN)
#define OFF_CHECK (OFF_TAG + 16u)
#define CHECK_LEN 8u
#define PT_VERSION 1u
#define PT_FLAG_BLIND 0x01u

_Static_assert(OFF_CHECK + CHECK_LEN == WL_BACKUP_FILE_LEN, "layout");

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }

/* 12 word indices (valid BIP39 checksum) -> the 128-bit entropy they encode. */
static wl_status_t pw_entropy(const uint16_t pw[WL_BACKUP_PW_WORDS], uint8_t ent[16]) {
  if (wl_mnemonic_check_indices(pw, WL_BACKUP_PW_WORDS) != WL_OK) return WL_ERR_CHECKSUM;
  memset(ent, 0, 16);
  for (unsigned bit = 0; bit < 128; bit++) {
    unsigned w = bit / 11, k = 10 - bit % 11;
    if (pw[w] >> k & 1u) ent[bit / 8] |= (uint8_t)(0x80u >> (bit % 8));
  }
  return WL_OK;
}

static bool kdf_ok(wl_kdf_t kdf, size_t work_size) {
  return kdf.m_kib >= WL_BACKUP_MIN_KIB && kdf.passes >= 1 && kdf.passes <= WL_BACKUP_MAX_PASSES &&
         (uint64_t)kdf.m_kib * 1024u <= work_size;
}

static void derive_key(const uint8_t ent[16], const uint8_t salt[16], wl_kdf_t kdf, void *work, uint8_t key[32]) {
  crypto_argon2_config cfg = {.algorithm = CRYPTO_ARGON2_ID, .nb_blocks = kdf.m_kib, .nb_passes = kdf.passes, .nb_lanes = 1};
  crypto_argon2_inputs in = {.pass = ent, .salt = salt, .pass_size = 16, .salt_size = WL_BACKUP_SALT_LEN};
  crypto_argon2(key, 32, work, cfg, in, crypto_argon2_no_extras);
  wl_wipe(work, (size_t)kdf.m_kib * 1024u); /* the work area holds password-derived blocks */
}

static void checksum(const uint8_t *f, uint8_t out[CHECK_LEN]) {
  uint8_t h[32];
  sha256_Raw(f, OFF_CHECK, h);
  memcpy(out, h, CHECK_LEN);
}

static bool valid_payload(const wl_backup_payload_t *p) {
  if (wl_mnemonic_valid_word_count(p->words) != 1) return false;
  return wl_mnemonic_check_indices(p->idx, p->words) == WL_OK;
}

wl_status_t wl_backup_seal(const wl_backup_payload_t *p, const uint16_t pw[WL_BACKUP_PW_WORDS], bool demo,
                           wl_kdf_t kdf, const uint8_t salt[WL_BACKUP_SALT_LEN],
                           const uint8_t nonce[WL_BACKUP_NONCE_LEN], void *work, size_t work_size,
                           uint8_t out[WL_BACKUP_FILE_LEN]) {
  if (!p || !pw || !salt || !nonce || !work || !out || !kdf_ok(kdf, work_size) || !valid_payload(p)) return WL_ERR_ARG;
  uint8_t ent[16], key[32], pt[PT_LEN];
  wl_status_t st = pw_entropy(pw, ent);
  if (st != WL_OK) return st;

  memset(out, 0, WL_BACKUP_FILE_LEN);
  memcpy(out, MAGIC, 8);
  out[8] = VERSION;
  out[9] = demo ? FLAG_DEMO : 0;
  out[10] = KDF_ARGON2ID;
  out[11] = 1;
  wr32(out + 12, kdf.m_kib);
  wr32(out + 16, kdf.passes);
  memcpy(out + 20, salt, WL_BACKUP_SALT_LEN);
  memcpy(out + 36, nonce, WL_BACKUP_NONCE_LEN);
  wr16(out + 48, PT_LEN);

  memset(pt, 0, sizeof(pt));
  pt[0] = PT_VERSION;
  pt[1] = p->words;
  pt[2] = p->blind_signing ? PT_FLAG_BLIND : 0;
  for (unsigned i = 0; i < p->words; i++) wr16(pt + 4 + 2 * i, p->idx[i]);

  derive_key(ent, salt, kdf, work, key);
  chacha20poly1305_ctx c;
  rfc7539_init(&c, key, nonce);
  rfc7539_auth(&c, out, HDR_LEN);
  chacha20poly1305_encrypt(&c, pt, out + OFF_CT, PT_LEN);
  rfc7539_finish(&c, HDR_LEN, PT_LEN, out + OFF_TAG);
  checksum(out, out + OFF_CHECK);

  wl_wipe(&c, sizeof(c));
  wl_wipe(ent, sizeof(ent));
  wl_wipe(key, sizeof(key));
  wl_wipe(pt, sizeof(pt));
  return WL_OK;
}

wl_status_t wl_backup_inspect(const uint8_t *f, size_t len, wl_backup_info_t *info) {
  if (info) memset(info, 0, sizeof(*info));
  if (!f || !info || len != WL_BACKUP_FILE_LEN || memcmp(f, MAGIC, 8) != 0) return WL_ERR_FORMAT;
  uint8_t c[CHECK_LEN];
  checksum(f, c);
  if (memcmp(c, f + OFF_CHECK, CHECK_LEN) != 0) return WL_ERR_DAMAGED; /* public data, no timing concern */
  if (f[8] != VERSION || (f[9] & ~FLAG_DEMO) != 0 || f[10] != KDF_ARGON2ID || f[11] != 1 || rd16(f + 48) != PT_LEN ||
      rd16(f + 50) != 0)
    return WL_ERR_FORMAT;
  info->demo = (f[9] & FLAG_DEMO) != 0;
  info->kdf.m_kib = rd32(f + 12);
  info->kdf.passes = rd32(f + 16);
  memcpy(info->salt, f + 20, WL_BACKUP_SALT_LEN);
  if (info->kdf.m_kib < WL_BACKUP_MIN_KIB || info->kdf.passes < 1 || info->kdf.passes > WL_BACKUP_MAX_PASSES) {
    memset(info, 0, sizeof(*info));
    return WL_ERR_FORMAT;
  }
  return WL_OK;
}

wl_status_t wl_backup_open(const uint8_t *f, size_t len, const uint16_t pw[WL_BACKUP_PW_WORDS], void *work,
                           size_t work_size, wl_backup_payload_t *p) {
  if (p) memset(p, 0, sizeof(*p));
  if (!p || !pw || !work) return WL_ERR_ARG;
  wl_backup_info_t info;
  wl_status_t st = wl_backup_inspect(f, len, &info);
  if (st != WL_OK) return st;
  if (!kdf_ok(info.kdf, work_size)) return WL_ERR_ARG; /* needs more memory than this device has */
  uint8_t ent[16], key[32], pt[PT_LEN], tag[16];
  st = pw_entropy(pw, ent);
  if (st != WL_OK) return st;

  derive_key(ent, info.salt, info.kdf, work, key);
  chacha20poly1305_ctx c;
  rfc7539_init(&c, key, f + 36);
  rfc7539_auth(&c, f, HDR_LEN);
  chacha20poly1305_decrypt(&c, f + OFF_CT, pt, PT_LEN);
  rfc7539_finish(&c, HDR_LEN, PT_LEN, tag);
  bool ok = wl_ct_equal(tag, f + OFF_TAG, 16);
  wl_wipe(&c, sizeof(c));
  wl_wipe(ent, sizeof(ent));
  wl_wipe(key, sizeof(key));
  if (!ok) {
    wl_wipe(pt, sizeof(pt));
    return WL_ERR_AUTH;
  }

  /* authentic: still validate every field (a future writer bug must not become a bad seed) */
  bool valid = pt[0] == PT_VERSION && (pt[2] & ~PT_FLAG_BLIND) == 0 && pt[3] == 0 && pt[1] <= 24;
  if (valid) {
    p->words = pt[1];
    p->blind_signing = (pt[2] & PT_FLAG_BLIND) != 0;
    for (unsigned i = 0; i < 24; i++) {
      uint16_t v = rd16(pt + 4 + 2 * i);
      if (i < p->words) p->idx[i] = v;
      else valid &= v == 0;
    }
    for (unsigned i = 52; i < PT_LEN; i++) valid &= pt[i] == 0;
    valid = valid && valid_payload(p);
  }
  wl_wipe(pt, sizeof(pt));
  if (!valid) {
    wl_wipe(p, sizeof(*p));
    return WL_ERR_FORMAT;
  }
  return WL_OK;
}
