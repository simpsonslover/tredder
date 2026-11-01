#ifndef WALLET_BTC_H
#define WALLET_BTC_H

/*
 * Bitcoin PSBT (BIP174 v0) analysis and signing.
 *
 * Rules (protocol/PROTOCOL.md):
 *  - EVERY input must be this wallet's: master fingerprint + path re-derived,
 *    the listed public key and the spent script must match what the device
 *    derives itself. Legacy (P2PKH) inputs need the full previous transaction,
 *    whose txid is checked, so the amount (and fee) cannot be lied about.
 *  - An output is "change" only if the device re-derives its script from a
 *    path on the internal chain (/1/i) of its own wallet. Everything else is
 *    shown as a payment.
 *  - SIGHASH_ALL (DEFAULT for taproot) only. Taproot: BIP86 key path only.
 */

#include <stddef.h>
#include <stdint.h>

#include "wallet/review.h"
#include "wallet/schemes.h"
#include "wallet/status.h"

#define WL_BTC_MAX_IN 16
#define WL_BTC_MAX_OUT 12

typedef struct {
  size_t map_start, map_end; /* key-value pairs of this input map (terminator at map_end) */
  uint8_t txid[32];
  uint32_t vout, sequence;
  uint64_t amount;
  uint8_t script[34];
  uint8_t script_len;
  uint8_t purpose; /* 44 / 49 / 84 / 86 */
  uint32_t path[5];
} wl_btc_in_t;

typedef struct {
  uint64_t amount;
  size_t script_off; /* into the PSBT buffer */
  size_t script_len;
} wl_btc_out_t;

typedef struct {
  uint32_t version, locktime;
  size_t nin, nout;
  wl_btc_in_t in[WL_BTC_MAX_IN];
  wl_btc_out_t out[WL_BTC_MAX_OUT];
  size_t outputs_start; /* first output map */
} wl_btc_tx_t;

/* Parse + check + build review pages. err gets a short reason on refusal. */
wl_verdict_t wl_btc_analyze(const uint8_t seed[64], wl_net_t net, const uint8_t *psbt, size_t len, wl_btc_tx_t *tx,
                            wl_review_t *rv, char *err, size_t errn);

/* Sign every input (after the user approved) and return the PSBT with the
   signatures added (PARTIAL_SIG / TAP_KEY_SIG). psbt must be the same bytes. */
wl_status_t wl_btc_sign(const uint8_t seed[64], const wl_btc_tx_t *tx, const uint8_t *psbt, size_t len, uint8_t *out,
                        size_t cap, size_t *outlen);

/* scriptPubKey -> address ("" + WL_ERR_ARG if not a standard script). */
wl_status_t wl_btc_script_address(const uint8_t *script, size_t n, wl_net_t net, char *out, size_t out_size);

#endif
