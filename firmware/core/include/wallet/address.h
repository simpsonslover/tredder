#ifndef WALLET_ADDRESS_H
#define WALLET_ADDRESS_H

#include <stddef.h>
#include <stdint.h>

#include "wallet/schemes.h"
#include "wallet/status.h"

#define WL_ADDRESS_MAX_LEN 96

/*
 * Derive the receive address for (scheme, net, account, change, index) from a
 * 64-byte BIP39 seed. All intermediate keys are wiped before returning.
 *   BTC  P2PKH / P2SH-P2WPKH (base58check), P2WPKH (bech32), P2TR (bech32m, BIP86 tweak)
 *   EVM  0x + EIP-55 checksummed hex
 *   SOL  base58 ed25519 public key
 */
wl_status_t wl_address(const uint8_t seed[64], wl_scheme_t scheme, wl_net_t net, uint32_t account,
                       uint32_t change, uint32_t index, char *out, size_t out_size);

/*
 * Account-level public key for watch-only use by the app (protocol get-accounts):
 * BTC schemes only, node at m/purpose'/coin'/account'. Public data only; the
 * master fingerprint lets PSBTs reference keys so the device can recognise
 * its own inputs and change outputs.
 */
typedef struct {
  uint8_t public_key[33];
  uint8_t chain_code[32];
  uint32_t master_fingerprint;
} wl_account_key_t;

wl_status_t wl_account_key(const uint8_t seed[64], wl_scheme_t scheme, wl_net_t net, uint32_t account,
                           wl_account_key_t *out);

#endif
