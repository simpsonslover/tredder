/*
 * Noise XX test vectors from the firmware's implementation (app/src/noise.c), written to
 * protocol/noise_vectors.json. The app's jest test runs its TypeScript Noise with the same
 * fixed keys and must reproduce every byte. Also self-checks both roles here, including
 * tampering. Built and run by tools/noise_vectors.py.
 */
#include <stdio.h>
#include <string.h>

#include "app/noise.h"

static int fails;
#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) printf("FAIL line %d: %s\n", __LINE__, #c), fails++; \
  } while (0)

static void key(uint8_t k[32], uint8_t seed) {
  for (int i = 0; i < 32; i++) k[i] = (uint8_t)(seed + i * 7);
}

static void hex(FILE *f, const char *name, const uint8_t *d, size_t n, int comma) {
  fprintf(f, "  \"%s\": \"", name);
  for (size_t i = 0; i < n; i++) fprintf(f, "%02x", d[i]);
  fprintf(f, "\"%s\n", comma ? "," : "");
}

int main(int argc, char **argv) {
  uint8_t is[32], ie[32], rs[32], re[32];
  key(is, 0x10), key(ie, 0x20), key(rs, 0x30), key(re, 0x40);
  noise_hs_t I, R;
  noise_xx_init(&I, true, is, ie);
  noise_xx_init(&R, false, rs, re);
  uint8_t m1[NOISE_MSG1], m2[NOISE_MSG2], m3[NOISE_MSG3];
  CHECK(noise_xx_write(&R, m1, sizeof(m1)) == 0); /* not the responder's turn */
  CHECK(noise_xx_write(&I, m1, sizeof(m1)) == NOISE_MSG1);
  CHECK(noise_xx_read(&R, m1, sizeof(m1)));
  CHECK(noise_xx_write(&R, m2, sizeof(m2)) == NOISE_MSG2);
  CHECK(noise_xx_read(&I, m2, sizeof(m2)));
  CHECK(noise_xx_write(&I, m3, sizeof(m3)) == NOISE_MSG3);
  CHECK(noise_xx_read(&R, m3, sizeof(m3)));

  noise_cs_t is_send, is_recv, rs_send, rs_recv;
  uint8_t ih[32], rh[32], i_rs[32], r_rs[32], is_pub[32], rs_pub[32];
  noise_public_key(is, is_pub);
  noise_public_key(rs, rs_pub);
  CHECK(noise_xx_split(&I, &is_send, &is_recv, ih, i_rs));
  CHECK(noise_xx_split(&R, &rs_send, &rs_recv, rh, r_rs));
  CHECK(!memcmp(ih, rh, 32));
  CHECK(!memcmp(i_rs, rs_pub, 32));
  CHECK(!memcmp(r_rs, is_pub, 32));
  uint32_t code = noise_pair_code(ih);

  /* transport, both directions, two messages each (nonce 0 and 1) */
  const char *a1 = "hello device", *a2 = "second request", *b1 = "hello app";
  uint8_t c_a1[64], c_a2[64], c_b1[64], tmp[64];
  size_t la1 = strlen(a1), la2 = strlen(a2), lb1 = strlen(b1);
  memcpy(c_a1, a1, la1), noise_seal(&is_send, c_a1, la1);
  memcpy(c_a2, a2, la2), noise_seal(&is_send, c_a2, la2);
  memcpy(c_b1, b1, lb1), noise_seal(&rs_send, c_b1, lb1);
  memcpy(tmp, c_a1, la1 + 16);
  CHECK(noise_open(&rs_recv, tmp, la1 + 16) && !memcmp(tmp, a1, la1));
  memcpy(tmp, c_a2, la2 + 16);
  tmp[3] ^= 1; /* tampered: rejected, counter not advanced */
  CHECK(!noise_open(&rs_recv, tmp, la2 + 16));
  memcpy(tmp, c_a2, la2 + 16);
  CHECK(noise_open(&rs_recv, tmp, la2 + 16) && !memcmp(tmp, a2, la2));
  memcpy(tmp, c_b1, lb1 + 16);
  CHECK(noise_open(&is_recv, tmp, lb1 + 16) && !memcmp(tmp, b1, lb1));
  memcpy(tmp, c_a1, la1 + 16); /* replay of nonce 0: rejected */
  CHECK(!noise_open(&rs_recv, tmp, la1 + 16));

  /* a tampered msg3 poisons the responder's handshake */
  noise_xx_init(&I, true, is, ie);
  noise_xx_init(&R, false, rs, re);
  uint8_t x1[NOISE_MSG1], x2[NOISE_MSG2], x3[NOISE_MSG3];
  noise_xx_write(&I, x1, sizeof(x1));
  noise_xx_read(&R, x1, sizeof(x1));
  noise_xx_write(&R, x2, sizeof(x2));
  noise_xx_read(&I, x2, sizeof(x2));
  noise_xx_write(&I, x3, sizeof(x3));
  x3[10] ^= 0x80;
  CHECK(!noise_xx_read(&R, x3, sizeof(x3)));
  CHECK(!noise_xx_split(&R, &rs_send, &rs_recv, rh, r_rs));

  if (argc > 1) {
    FILE *f = fopen(argv[1], "w");
    if (!f) return 2;
    fprintf(f, "{\n  \"protocol\": \"Noise_XX_25519_ChaChaPoly_SHA256\",\n  \"prologue\": \"tredder/1\",\n");
    hex(f, "initiatorStatic", is, 32, 1), hex(f, "initiatorEphemeral", ie, 32, 1);
    hex(f, "responderStatic", rs, 32, 1), hex(f, "responderEphemeral", re, 32, 1);
    hex(f, "msg1", m1, sizeof(m1), 1), hex(f, "msg2", m2, sizeof(m2), 1), hex(f, "msg3", m3, sizeof(m3), 1);
    hex(f, "handshakeHash", ih, 32, 1);
    fprintf(f, "  \"pairCode\": %lu,\n", (unsigned long)code);
    fprintf(f, "  \"toResponder\": [\"%s\", \"%s\"],\n  \"toInitiator\": [\"%s\"],\n", a1, a2, b1);
    hex(f, "toResponder0", c_a1, la1 + 16, 1), hex(f, "toResponder1", c_a2, la2 + 16, 1);
    hex(f, "toInitiator0", c_b1, lb1 + 16, 0);
    fprintf(f, "}\n");
    fclose(f);
  }
  if (fails) printf("noise: %d FAILURES\n", fails);
  else printf("noise: all checks pass (pairing code %06lu)\n", (unsigned long)code);
  return fails != 0;
}
