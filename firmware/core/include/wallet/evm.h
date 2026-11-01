#ifndef WALLET_EVM_H
#define WALLET_EVM_H

/*
 * EVM (Ethereum, Base, BNB Chain, Polygon, Arbitrum) EIP-1559 transactions.
 *
 * The request carries the unsigned typed transaction 0x02 || rlp([...]). The
 * device decodes it strictly (canonical RLP, nothing trailing), checks the
 * chain id, shows recipient/amount/fee and the decoded call (ERC-20
 * transfer / approve, with a loud warning for unlimited approvals), and
 * refuses anything it cannot decode unless blind signing is switched on.
 * It signs keccak256(those exact bytes).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wallet/review.h"
#include "wallet/status.h"

typedef struct {
  char symbol[12];  /* "" = none (app hint, shown as unverified) */
  int decimals;     /* -1 = none */
} wl_evm_hints_t;

wl_verdict_t wl_evm_analyze(const uint8_t *tx, size_t len, uint64_t chain_id, const char *from, uint32_t account,
                            bool blind_signing, const wl_evm_hints_t *hints, wl_review_t *rv, char *err, size_t errn);

wl_status_t wl_evm_sign(const uint8_t priv[32], const uint8_t *tx, size_t len, uint8_t r[32], uint8_t s[32],
                        uint8_t *y_parity);

/* Case-insensitive 0x-address comparison (EIP-55 case is only a checksum). */
bool wl_evm_same_address(const char *a, const char *b);

#endif
