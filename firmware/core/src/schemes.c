#include "wallet/schemes.h"

#include <stdio.h>

static const wl_scheme_info_t SCHEMES[WL_SCHEME_COUNT] = {
    {WL_SCHEME_BTC_LEGACY, WL_CHAIN_BTC, WL_CURVE_SECP256K1, "btc-p2pkh", "Bitcoin Legacy",
     "BIP44: Electrum legacy, older wallets", 1, 255},
    {WL_SCHEME_BTC_NESTED, WL_CHAIN_BTC, WL_CURVE_SECP256K1, "btc-p2sh-p2wpkh", "Bitcoin Nested SegWit",
     "BIP49: Trezor/Ledger 'SegWit', Samourai", 1, 255},
    {WL_SCHEME_BTC_NATIVE, WL_CHAIN_BTC, WL_CURVE_SECP256K1, "btc-p2wpkh", "Bitcoin Native SegWit",
     "BIP84: most current wallets", 1, 255},
    {WL_SCHEME_BTC_TAPROOT, WL_CHAIN_BTC, WL_CURVE_SECP256K1, "btc-p2tr", "Bitcoin Taproot",
     "BIP86: Sparrow, Trezor, Ledger, Xverse", 1, 255},
    {WL_SCHEME_EVM_STANDARD, WL_CHAIN_EVM, WL_CURVE_SECP256K1, "evm-bip44", "EVM",
     "MetaMask, Rabby, Trust, Coinbase Wallet", 0, 255},
    {WL_SCHEME_EVM_LEDGER_LIVE, WL_CHAIN_EVM, WL_CURVE_SECP256K1, "evm-ledger-live", "EVM (Ledger Live)",
     "Ledger Live", 0, 255},
    {WL_SCHEME_SOL_STANDARD, WL_CHAIN_SOL, WL_CURVE_ED25519, "sol-bip44-change", "Solana",
     "Phantom, Solflare, Backpack", 0, 255},
    {WL_SCHEME_SOL_BIP44, WL_CHAIN_SOL, WL_CURVE_ED25519, "sol-bip44", "Solana (BIP44)",
     "older Solflare, Trust Wallet", 0, 255},
    {WL_SCHEME_SOL_BIP44_ROOT, WL_CHAIN_SOL, WL_CURVE_ED25519, "sol-bip44-root", "Solana (root)",
     "some older wallets", 0, 0},
    {WL_SCHEME_SOL_CLI_SEED, WL_CHAIN_SOL, WL_CURVE_ED25519, "sol-cli", "Solana (CLI seed)",
     "solana-keygen without derivation path", 0, 0},
};

const wl_scheme_info_t *wl_scheme_info(wl_scheme_t s) {
  return (unsigned)s < WL_SCHEME_COUNT ? &SCHEMES[s] : NULL;
}

wl_status_t wl_scheme_path(wl_scheme_t s, wl_net_t net, uint32_t account, uint32_t change,
                           uint32_t index, uint32_t path[WL_MAX_PATH_DEPTH], size_t *depth) {
  const wl_scheme_info_t *info = wl_scheme_info(s);
  if (!info || (net != WL_NET_MAIN && net != WL_NET_TEST)) return WL_ERR_ARG;
  if (account >= WL_HARDENED || index >= WL_HARDENED || account > info->max_account) return WL_ERR_ARG;
  if (!info->uses_index && (change || index)) return WL_ERR_ARG;
  if (info->uses_index && change > 1) return WL_ERR_ARG;

  const uint32_t H = WL_HARDENED;
  size_t d = 0;
  switch (s) {
    case WL_SCHEME_BTC_LEGACY:
    case WL_SCHEME_BTC_NESTED:
    case WL_SCHEME_BTC_NATIVE:
    case WL_SCHEME_BTC_TAPROOT: {
      static const uint32_t purpose[] = {44, 49, 84, 86};
      path[d++] = H | purpose[s - WL_SCHEME_BTC_LEGACY];
      path[d++] = H | (net == WL_NET_TEST ? 1u : 0u);
      path[d++] = H | account;
      path[d++] = change;
      path[d++] = index;
      break;
    }
    case WL_SCHEME_EVM_STANDARD:  /* m/44'/60'/0'/0/n */
      path[d++] = H | 44; path[d++] = H | 60; path[d++] = H | 0; path[d++] = 0; path[d++] = account;
      break;
    case WL_SCHEME_EVM_LEDGER_LIVE:  /* m/44'/60'/n'/0/0 */
      path[d++] = H | 44; path[d++] = H | 60; path[d++] = H | account; path[d++] = 0; path[d++] = 0;
      break;
    case WL_SCHEME_SOL_STANDARD:  /* m/44'/501'/n'/0' */
      path[d++] = H | 44; path[d++] = H | 501; path[d++] = H | account; path[d++] = H | 0;
      break;
    case WL_SCHEME_SOL_BIP44:  /* m/44'/501'/n' */
      path[d++] = H | 44; path[d++] = H | 501; path[d++] = H | account;
      break;
    case WL_SCHEME_SOL_BIP44_ROOT:  /* m/44'/501' */
      path[d++] = H | 44; path[d++] = H | 501;
      break;
    case WL_SCHEME_SOL_CLI_SEED:
      break;
    default:
      return WL_ERR_ARG;
  }
  *depth = d;
  return WL_OK;
}

wl_status_t wl_path_to_string(const uint32_t *path, size_t depth, char *out, size_t out_size) {
  int n = snprintf(out, out_size, "m");
  for (size_t i = 0; i < depth && n > 0 && (size_t)n < out_size; i++) {
    n += snprintf(out + n, out_size - (size_t)n, "/%lu%s", (unsigned long)(path[i] & ~WL_HARDENED),
                  (path[i] & WL_HARDENED) ? "'" : "");
  }
  return (n > 0 && (size_t)n < out_size) ? WL_OK : WL_ERR_BUFFER;
}
