#ifndef APP_NOISE_H
#define APP_NOISE_H

/*
 * Noise_XX_25519_ChaChaPoly_SHA256 (noiseprotocol.org rev 34), prologue "tredder/1",
 * empty handshake payloads. protocol/PROTOCOL.md section 3.
 *
 *   msg1  I -> R  e                      32 bytes
 *   msg2  R -> I  e, ee, s, es           96 bytes
 *   msg3  I -> R  s, se                  64 bytes
 *
 * The app is the initiator, the device the responder. Ephemeral keys are passed in by the
 * caller (fresh random on the device; fixed in the cross-implementation test vectors).
 * Transport messages: ChaChaPoly with nonce = 4 zero bytes || 64-bit little-endian counter,
 * empty associated data, 16-byte tag appended. Unlike the Noise spec they may exceed 65535
 * bytes (the framing layer limits the size).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NOISE_KEY 32u
#define NOISE_TAG 16u
#define NOISE_MSG1 32u
#define NOISE_MSG2 96u
#define NOISE_MSG3 64u

typedef struct {
  uint8_t k[32];
  uint64_t n;
} noise_cs_t;

typedef struct {
  uint8_t ck[32], h[32], k[32];
  bool has_k;
  uint64_t n;
  bool initiator;
  int step; /* next message: 0 = msg1, 1 = msg2, 2 = msg3, 3 = done, -1 = failed */
  uint8_t s[32], s_pub[32], e[32], e_pub[32], re[32], rs[32];
} noise_hs_t;

void noise_xx_init(noise_hs_t *hs, bool initiator, const uint8_t s_priv[32], const uint8_t e_priv[32]);
/* Write the next handshake message if it is ours: returns its length, 0 on error. */
size_t noise_xx_write(noise_hs_t *hs, uint8_t *out, size_t cap);
/* Read the next handshake message if it is theirs. Any failure poisons the handshake. */
bool noise_xx_read(noise_hs_t *hs, const uint8_t *in, size_t n);
/* After msg3: the two transport keys, the handshake hash and the peer's static key.
   Wipes the handshake state. */
bool noise_xx_split(noise_hs_t *hs, noise_cs_t *send, noise_cs_t *recv, uint8_t hash[32], uint8_t remote_static[32]);

/* In place: buf holds n bytes of plaintext and has room for NOISE_TAG more. */
void noise_seal(noise_cs_t *cs, uint8_t *buf, size_t n);
/* In place: n includes the tag. On success the plaintext is buf[0 .. n-NOISE_TAG). */
bool noise_open(noise_cs_t *cs, uint8_t *buf, size_t n);

void noise_public_key(const uint8_t priv[32], uint8_t pub[32]);
/* The 6-digit pairing code both sides show: SHA-256("tredder pairing code" || h), first 4
   bytes big-endian, mod 1 000 000. */
uint32_t noise_pair_code(const uint8_t hash[32]);

#endif
