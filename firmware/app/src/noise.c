/*
 * Noise_XX_25519_ChaChaPoly_SHA256 (see app/noise.h). Built on trezor-crypto: curve25519-donna,
 * RFC 7539 ChaCha20-Poly1305, SHA-256 / HMAC-SHA-256. The app's TypeScript implementation
 * (app/src/protocol/noise.ts) is checked against this one with shared test vectors.
 */
#include "app/noise.h"

#include <string.h>

#include "chacha20poly1305/rfc7539.h"
#include "ed25519-donna/ed25519.h"
#include "hmac.h"
#include "sha2.h"
#include "wallet/secure.h"

static const char PROTOCOL_NAME[] = "Noise_XX_25519_ChaChaPoly_SHA256"; /* exactly 32 bytes */
static const char PROLOGUE[] = "tredder/1";

_Static_assert(sizeof(PROTOCOL_NAME) - 1 == 32, "protocol name is used as h directly");

/* ---------------------------------------------------------------- primitives */
static void nonce12(uint64_t n, uint8_t out[12]) {
  memset(out, 0, 4);
  for (int i = 0; i < 8; i++) out[4 + i] = (uint8_t)(n >> (8 * i));
}

static void aead(bool enc, const uint8_t k[32], uint64_t n, const uint8_t *ad, size_t adn, uint8_t *buf, size_t pn,
                 uint8_t tag[16]) {
  uint8_t iv[12];
  chacha20poly1305_ctx c;
  nonce12(n, iv);
  rfc7539_init(&c, k, iv);
  if (adn) rfc7539_auth(&c, ad, adn);
  if (enc) chacha20poly1305_encrypt(&c, buf, buf, pn);
  else chacha20poly1305_decrypt(&c, buf, buf, pn);
  rfc7539_finish(&c, (int64_t)adn, (int64_t)pn, tag);
  wl_wipe(&c, sizeof(c));
}

/* HKDF with 2 outputs (Noise section 4.3), chaining key = salt. */
static void hkdf2(const uint8_t ck[32], const uint8_t *ikm, size_t n, uint8_t o1[32], uint8_t o2[32]) {
  uint8_t prk[32], t[33];
  hmac_sha256(ck, 32, ikm, (uint32_t)n, prk);
  t[0] = 1;
  hmac_sha256(prk, 32, t, 1, o1);
  memcpy(t, o1, 32);
  t[32] = 2;
  hmac_sha256(prk, 32, t, 33, o2);
  wl_wipe(prk, sizeof(prk));
  wl_wipe(t, sizeof(t));
}

void noise_public_key(const uint8_t priv[32], uint8_t pub[32]) { curve25519_scalarmult_basepoint(pub, priv); }

static void dh(const uint8_t priv[32], const uint8_t pub[32], uint8_t out[32]) { curve25519_scalarmult(out, priv, pub); }

/* ---------------------------------------------------------------- symmetric state */
static void mix_hash(noise_hs_t *hs, const uint8_t *d, size_t n) {
  SHA256_CTX c;
  sha256_Init(&c);
  sha256_Update(&c, hs->h, 32);
  sha256_Update(&c, d, n);
  sha256_Final(&c, hs->h);
}

static void mix_key(noise_hs_t *hs, const uint8_t ikm[32]) {
  uint8_t k[32];
  hkdf2(hs->ck, ikm, 32, hs->ck, k);
  memcpy(hs->k, k, 32);
  hs->has_k = true;
  hs->n = 0;
  wl_wipe(k, sizeof(k));
}

static void mix_dh(noise_hs_t *hs, const uint8_t priv[32], const uint8_t pub[32]) {
  uint8_t shared[32];
  dh(priv, pub, shared);
  mix_key(hs, shared);
  wl_wipe(shared, sizeof(shared));
}

/* out = encrypt(p) (+ tag once a key exists); returns the bytes written. */
static size_t encrypt_and_hash(noise_hs_t *hs, const uint8_t *p, size_t n, uint8_t *out) {
  memmove(out, p, n);
  if (!hs->has_k) {
    mix_hash(hs, out, n);
    return n;
  }
  aead(true, hs->k, hs->n++, hs->h, 32, out, n, out + n);
  mix_hash(hs, out, n + NOISE_TAG);
  return n + NOISE_TAG;
}

/* in: n bytes of ciphertext (+ tag); plaintext to out (n - tag bytes). */
static bool decrypt_and_hash(noise_hs_t *hs, const uint8_t *in, size_t n, uint8_t *out) {
  if (!hs->has_k) {
    memcpy(out, in, n);
    mix_hash(hs, in, n);
    return true;
  }
  if (n < NOISE_TAG) return false;
  uint8_t tmp[64 + NOISE_TAG], tag[16];
  size_t pn = n - NOISE_TAG;
  if (n > sizeof(tmp)) return false;
  memcpy(tmp, in, n);
  aead(false, hs->k, hs->n, hs->h, 32, tmp, pn, tag);
  bool ok = wl_ct_equal(tag, in + pn, NOISE_TAG);
  if (ok) {
    hs->n++;
    mix_hash(hs, in, n);
    memcpy(out, tmp, pn);
  }
  wl_wipe(tmp, sizeof(tmp));
  return ok;
}

/* ---------------------------------------------------------------- XX handshake */
void noise_xx_init(noise_hs_t *hs, bool initiator, const uint8_t s_priv[32], const uint8_t e_priv[32]) {
  memset(hs, 0, sizeof(*hs));
  memcpy(hs->h, PROTOCOL_NAME, 32);
  memcpy(hs->ck, hs->h, 32);
  mix_hash(hs, (const uint8_t *)PROLOGUE, sizeof(PROLOGUE) - 1);
  hs->initiator = initiator;
  memcpy(hs->s, s_priv, 32);
  memcpy(hs->e, e_priv, 32);
  noise_public_key(hs->s, hs->s_pub);
  noise_public_key(hs->e, hs->e_pub);
}

static bool ours(const noise_hs_t *hs) { return hs->step == 1 ? !hs->initiator : hs->initiator; }

size_t noise_xx_write(noise_hs_t *hs, uint8_t *out, size_t cap) {
  if (hs->step < 0 || hs->step > 2 || !ours(hs)) return 0;
  size_t n = 0;
  if (hs->step == 0) { /* -> e */
    if (cap < NOISE_MSG1) return 0;
    memcpy(out, hs->e_pub, 32);
    mix_hash(hs, hs->e_pub, 32);
    n = 32;
    n += encrypt_and_hash(hs, NULL, 0, out + n);
  } else if (hs->step == 1) { /* <- e, ee, s, es */
    if (cap < NOISE_MSG2) return 0;
    memcpy(out, hs->e_pub, 32);
    mix_hash(hs, hs->e_pub, 32);
    mix_dh(hs, hs->e, hs->re);                        /* ee */
    n = 32 + encrypt_and_hash(hs, hs->s_pub, 32, out + 32); /* s */
    mix_dh(hs, hs->s, hs->re);                        /* es = DH(responder s, initiator e) */
    n += encrypt_and_hash(hs, NULL, 0, out + n);
  } else { /* -> s, se */
    if (cap < NOISE_MSG3) return 0;
    n = encrypt_and_hash(hs, hs->s_pub, 32, out);
    mix_dh(hs, hs->s, hs->re); /* se = DH(initiator s, responder e) */
    n += encrypt_and_hash(hs, NULL, 0, out + n);
  }
  hs->step++;
  return n;
}

bool noise_xx_read(noise_hs_t *hs, const uint8_t *in, size_t n) {
  uint8_t empty[1];
  bool ok = false;
  if (hs->step < 0 || hs->step > 2 || ours(hs)) goto done;
  if (hs->step == 0) {
    if (n != NOISE_MSG1) goto done;
    memcpy(hs->re, in, 32);
    mix_hash(hs, hs->re, 32);
    ok = decrypt_and_hash(hs, in + 32, 0, empty);
  } else if (hs->step == 1) {
    if (n != NOISE_MSG2) goto done;
    memcpy(hs->re, in, 32);
    mix_hash(hs, hs->re, 32);
    mix_dh(hs, hs->e, hs->re); /* ee */
    if (!decrypt_and_hash(hs, in + 32, 32 + NOISE_TAG, hs->rs)) goto done;
    mix_dh(hs, hs->e, hs->rs); /* es = DH(initiator e, responder s) */
    ok = decrypt_and_hash(hs, in + 80, NOISE_TAG, empty);
  } else {
    if (n != NOISE_MSG3) goto done;
    if (!decrypt_and_hash(hs, in, 32 + NOISE_TAG, hs->rs)) goto done;
    mix_dh(hs, hs->e, hs->rs); /* se = DH(responder e, initiator s) */
    ok = decrypt_and_hash(hs, in + 48, NOISE_TAG, empty);
  }
done:
  if (ok) hs->step++;
  else hs->step = -1;
  return ok;
}

bool noise_xx_split(noise_hs_t *hs, noise_cs_t *send, noise_cs_t *recv, uint8_t hash[32], uint8_t remote_static[32]) {
  if (hs->step != 3) return false;
  uint8_t k1[32], k2[32];
  hkdf2(hs->ck, NULL, 0, k1, k2);
  memcpy(send->k, hs->initiator ? k1 : k2, 32);
  memcpy(recv->k, hs->initiator ? k2 : k1, 32);
  send->n = recv->n = 0;
  memcpy(hash, hs->h, 32);
  memcpy(remote_static, hs->rs, 32);
  wl_wipe(k1, sizeof(k1));
  wl_wipe(k2, sizeof(k2));
  wl_wipe(hs, sizeof(*hs));
  return true;
}

/* ---------------------------------------------------------------- transport */
void noise_seal(noise_cs_t *cs, uint8_t *buf, size_t n) { aead(true, cs->k, cs->n++, NULL, 0, buf, n, buf + n); }

bool noise_open(noise_cs_t *cs, uint8_t *buf, size_t n) {
  if (n < NOISE_TAG) return false;
  uint8_t tag[16], saved[NOISE_TAG];
  size_t pn = n - NOISE_TAG;
  memcpy(saved, buf + pn, NOISE_TAG);
  aead(false, cs->k, cs->n, NULL, 0, buf, pn, tag);
  if (!wl_ct_equal(tag, saved, NOISE_TAG)) {
    wl_wipe(buf, pn); /* never leave unauthenticated plaintext behind */
    return false;
  }
  cs->n++;
  return true;
}

uint32_t noise_pair_code(const uint8_t hash[32]) {
  static const char L[] = "tredder pairing code";
  uint8_t d[32];
  SHA256_CTX c;
  sha256_Init(&c);
  sha256_Update(&c, (const uint8_t *)L, sizeof(L) - 1);
  sha256_Update(&c, hash, 32);
  sha256_Final(&c, d);
  return ((uint32_t)d[0] << 24 | (uint32_t)d[1] << 16 | (uint32_t)d[2] << 8 | d[3]) % 1000000u;
}
