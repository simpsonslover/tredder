#ifndef WALLET_SCHEMES_H
#define WALLET_SCHEMES_H

#include <stddef.h>
#include <stdint.h>

#include "wallet/status.h"

/*
 * Derivation schemes = (chain, path layout, address format) as used by the
 * popular wallets, so an imported seed shows the same addresses/funds.
 * To add a chain: add scheme ids here, a row in schemes.c, and an encoder in
 * address.c. Everything else (UI lists, app protocol, scanning) iterates the table.
 */

typedef enum { WL_CHAIN_BTC = 0, WL_CHAIN_EVM, WL_CHAIN_SOL, WL_CHAIN_COUNT } wl_chain_t;
typedef enum { WL_NET_MAIN = 0, WL_NET_TEST } wl_net_t;  /* BTC testnet / Sepolia / Solana devnet */
typedef enum { WL_CURVE_SECP256K1 = 0, WL_CURVE_ED25519 } wl_curve_t;

typedef enum {
  /* Bitcoin: m/purpose'/coin'/account'/change/index, coin 0 main / 1 test */
  WL_SCHEME_BTC_LEGACY = 0,     /* BIP44  P2PKH        1... / m,n...       */
  WL_SCHEME_BTC_NESTED,         /* BIP49  P2SH-P2WPKH  3... / 2...         */
  WL_SCHEME_BTC_NATIVE,         /* BIP84  P2WPKH       bc1q / tb1q         */
  WL_SCHEME_BTC_TAPROOT,        /* BIP86  P2TR         bc1p / tb1p         */
  /* EVM (Ethereum, Base, BNB Chain, Polygon, Arbitrum: same addresses) */
  WL_SCHEME_EVM_STANDARD,       /* MetaMask, Trust, Rabby: m/44'/60'/0'/0/n */
  WL_SCHEME_EVM_LEDGER_LIVE,    /* Ledger Live:            m/44'/60'/n'/0/0 */
  /* Solana (ed25519 / SLIP-10, hardened only) */
  WL_SCHEME_SOL_STANDARD,       /* Phantom, Solflare, Backpack: m/44'/501'/n'/0' */
  WL_SCHEME_SOL_BIP44,          /* older Solflare / Trust:      m/44'/501'/n'    */
  WL_SCHEME_SOL_BIP44_ROOT,     /* some older wallets:          m/44'/501'       */
  WL_SCHEME_SOL_CLI_SEED,       /* solana-keygen without path: first 32 bytes of BIP39 seed */
  WL_SCHEME_COUNT
} wl_scheme_t;

typedef struct {
  wl_scheme_t id;
  wl_chain_t chain;
  wl_curve_t curve;
  const char *name;       /* short id used by the app protocol, e.g. "btc-p2wpkh" */
  const char *label;      /* shown on screen */
  const char *wallets;    /* which wallets use it (shown during import scan) */
  uint8_t uses_index;     /* 1 if account+change+index all matter (BTC) */
  uint8_t max_account;    /* 0 = only account 0 exists */
} wl_scheme_info_t;

const wl_scheme_info_t *wl_scheme_info(wl_scheme_t s);

#define WL_MAX_PATH_DEPTH 5
#define WL_HARDENED 0x80000000u

/*
 * Build the path for `account` (the number the source wallet shows as
 * "Account N"), and for BTC also change (0 receive, 1 change) and index.
 * Non-BTC schemes require change == index == 0.
 * WL_SCHEME_SOL_CLI_SEED yields depth 0 (no derivation).
 */
wl_status_t wl_scheme_path(wl_scheme_t s, wl_net_t net, uint32_t account, uint32_t change,
                           uint32_t index, uint32_t path[WL_MAX_PATH_DEPTH], size_t *depth);

/* "m/84'/0'/0'/0/5" (for display / logs). */
wl_status_t wl_path_to_string(const uint32_t *path, size_t depth, char *out, size_t out_size);

#endif
