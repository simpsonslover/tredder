#ifndef WALLET_KEYS_H
#define WALLET_KEYS_H

/*
 * Private-key derivation for signing. Every function wipes its temporaries;
 * callers wipe the keys they receive as soon as the signature is made.
 */

#include <stddef.h>
#include <stdint.h>

#include "bip32.h" /* trezor-crypto HDNode */
#include "wallet/schemes.h"
#include "wallet/status.h"

/* secp256k1 node at an explicit BIP32 path, public key filled. */
wl_status_t wl_node_at(const uint8_t seed[64], const uint32_t *path, size_t depth, HDNode *out);

uint32_t wl_master_fingerprint(const uint8_t seed[64]);

/* Private key for (scheme, net, account, change, index): secp256k1 for BTC/EVM,
   ed25519 for Solana (SLIP-10; sol-cli = first 32 seed bytes). */
wl_status_t wl_scheme_privkey(const uint8_t seed[64], wl_scheme_t s, wl_net_t net, uint32_t account,
                              uint32_t change, uint32_t index, uint8_t priv[32]);

/* Solana: 32-byte ed25519 public key of a scheme/account. */
wl_status_t wl_sol_pubkey(const uint8_t seed[64], wl_scheme_t s, uint32_t account, uint8_t pub[32]);

/* BIP86: x-only output key Q = P + H_TapTweak(P_x)G for a compressed internal key. */
wl_status_t wl_taproot_output_key(const uint8_t pub33[33], uint8_t out_x[32]);
/* The tweak itself: tagged_hash("TapTweak", P_x). */
wl_status_t wl_taproot_tweak(const uint8_t pub33[33], uint8_t tweak[32]);

/* Shared libsecp256k1 signing context (preallocated, randomised once). */
struct secp256k1_context_struct;
const struct secp256k1_context_struct *wl_secp_ctx(void);

/* Look a scheme up by its protocol name ("btc-p2wpkh"). */
int wl_scheme_by_name(const char *name); /* -1 if unknown */

#endif
