#ifndef WALLET_ENTROPY_H
#define WALLET_ENTROPY_H

#include <stddef.h>
#include <stdint.h>

#include "sha2.h" /* trezor-crypto */
#include "wallet/status.h"

/*
 * Seed entropy: RP2350 hardware TRNG mixed with the secure element's RNG.
 *
 * Design
 *  - Each source is hashed into its own SHA-512 state and gets its own
 *    entropy credit. Extraction requires >= 256 credited bits from BOTH the
 *    TRNG and the SE, so either source alone is enough for a strong seed and a
 *    weak or backdoored source cannot weaken the result.
 *  - Output = HMAC-SHA512(key = H(TRNG pool), msg = domain || H(SE pool) || H(extra)).
 *    The SE never sees the TRNG data (it never leaves the MCU), so a malicious
 *    SE cannot choose its output to cancel the TRNG contribution.
 *  - Raw TRNG bytes go through NIST SP 800-90B continuous health tests
 *    (repetition count + adaptive proportion). A failure latches: the pool is
 *    wiped and refuses to produce output until re-initialised.
 *  - Optional user entropy (e.g. dice rolls) can be mixed in as "extra"; it
 *    gets no credit, so it can only help.
 *  - The pool is wiped after a successful extraction.
 */

#define WL_ENTROPY_MIN_BITS 256u

/* Raw TRNG samples are credited conservatively at 1 bit per byte. */
#define WL_TRNG_BITS_PER_BYTE 1u
/* SP 800-90B cutoffs for H = 1 bit/sample, alpha = 2^-30 */
#define WL_RCT_CUTOFF 31u        /* identical consecutive samples */
#define WL_APT_WINDOW 512u
#define WL_APT_CUTOFF 410u       /* occurrences of the first sample in a window */

typedef struct {
  SHA512_CTX pool[3];      /* trng, se, extra */
  uint32_t credited_bits[2];
  uint8_t rct_last;
  uint32_t rct_count;
  uint8_t apt_first;
  uint32_t apt_seen, apt_count;
  uint8_t failed;
  uint8_t initialised;
} wl_entropy_t;

void wl_entropy_init(wl_entropy_t *e);

/* Raw bytes straight from the RP2350 TRNG peripheral. Runs health tests. */
wl_status_t wl_entropy_add_trng(wl_entropy_t *e, const uint8_t *raw, size_t n);

/* Output of the secure element's TRNG (already conditioned by the SE).
   credited_bits must be <= 8 * n. */
wl_status_t wl_entropy_add_se(wl_entropy_t *e, const uint8_t *data, size_t n, uint32_t credited_bits);

/* Uncredited user entropy (dice, etc.). */
void wl_entropy_add_extra(wl_entropy_t *e, const uint8_t *data, size_t n);

/* Produce up to 64 bytes. Fails unless both sources are credited >= 256 bits
   and no health test failed. Wipes the pool afterwards either way. */
wl_status_t wl_entropy_extract(wl_entropy_t *e, uint8_t *out, size_t n);

void wl_entropy_wipe(wl_entropy_t *e);

#endif
