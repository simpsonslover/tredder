#ifndef WALLET_SOL_H
#define WALLET_SOL_H

/*
 * Solana message (legacy and v0) analysis and signing.
 *  - The fee payer (first account) must be this device's key and it must be
 *    the only required signer.
 *  - Decoded: System transfer, SPL Token / Token-2022 TransferChecked,
 *    Associated Token Account creation, Compute Budget (shown as priority fee).
 *  - Anything else, and address lookup tables, is refused unless blind
 *    signing is on.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wallet/review.h"
#include "wallet/status.h"

wl_verdict_t wl_sol_analyze(const uint8_t *msg, size_t len, const uint8_t me[32], bool testnet, uint32_t account,
                            bool blind_signing, wl_review_t *rv, char *err, size_t errn);

/* ed25519 signature over the message bytes. */
void wl_sol_sign(const uint8_t sk[32], const uint8_t *msg, size_t len, uint8_t sig[64]);

void wl_sol_address(const uint8_t pub[32], char *out, size_t out_size);

#endif
