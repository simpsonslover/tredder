#include "wallet/address.h"

#include <string.h>

#include "address.h"     /* trezor-crypto: ethereum_address_checksum */
#include "base58.h"
#include "bip32.h"
#include "curves.h"
#include "ecdsa.h"
#include "ed25519-donna/ed25519.h"
#include "segwit_addr.h"
#include "wallet/keys.h"
#include "wallet/secure.h"

#if USE_BIP32_CACHE
#error "USE_BIP32_CACHE must be 0: the cache keeps private nodes in RAM"
#endif
#if !USE_ETHEREUM
#error "USE_ETHEREUM must be 1 (EIP-55 checksum and keccak pubkey hash)"
#endif

static wl_status_t derive(const uint8_t seed[64], wl_scheme_t s, wl_net_t net, uint32_t account,
                          uint32_t change, uint32_t index, HDNode *node) {
  uint32_t path[WL_MAX_PATH_DEPTH];
  size_t depth = 0;
  wl_status_t st = wl_scheme_path(s, net, account, change, index, path, &depth);
  if (st != WL_OK) return st;
  const char *curve = wl_scheme_info(s)->curve == WL_CURVE_ED25519 ? ED25519_NAME : SECP256K1_NAME;
  if (!hdnode_from_seed(seed, 64, curve, node)) return WL_ERR_CRYPTO;
  for (size_t i = 0; i < depth; i++) {
    if (!hdnode_private_ckd(node, path[i])) return WL_ERR_CRYPTO;
  }
  if (hdnode_fill_public_key(node) != 0) return WL_ERR_CRYPTO; /* NB: 0 = success here */
  return WL_OK;
}

static wl_status_t btc_address(const HDNode *node, wl_scheme_t s, wl_net_t net, char *out, size_t out_size) {
  const int test = net == WL_NET_TEST;
  const char *hrp = test ? "tb" : "bc";
  uint8_t prog[32];
  switch (s) {
    case WL_SCHEME_BTC_LEGACY:
      if (out_size < 36) return WL_ERR_BUFFER;
      ecdsa_get_address(node->public_key, test ? 0x6F : 0x00, HASHER_SHA2_RIPEMD, HASHER_SHA2D, out, (int)out_size);
      return WL_OK;
    case WL_SCHEME_BTC_NESTED:
      if (out_size < 36) return WL_ERR_BUFFER;
      ecdsa_get_address_segwit_p2sh(node->public_key, test ? 0xC4 : 0x05, HASHER_SHA2_RIPEMD, HASHER_SHA2D, out,
                                    (int)out_size);
      return WL_OK;
    case WL_SCHEME_BTC_NATIVE:
      if (out_size < 91) return WL_ERR_BUFFER; /* segwit_addr_encode needs up to 90 + NUL */
      ecdsa_get_pubkeyhash(node->public_key, HASHER_SHA2_RIPEMD, prog);
      return segwit_addr_encode(out, hrp, 0, prog, 20) ? WL_OK : WL_ERR_CRYPTO;
    case WL_SCHEME_BTC_TAPROOT:
      if (out_size < 91) return WL_ERR_BUFFER;
      if (wl_taproot_output_key(node->public_key, prog) != WL_OK) return WL_ERR_CRYPTO;
      return segwit_addr_encode(out, hrp, 1, prog, 32) ? WL_OK : WL_ERR_CRYPTO;
    default:
      return WL_ERR_ARG;
  }
}

static wl_status_t evm_address(const HDNode *node, char *out, size_t out_size) {
  uint8_t h[20];
  if (out_size < 43) return WL_ERR_BUFFER;
  if (!hdnode_get_ethereum_pubkeyhash(node, h)) return WL_ERR_CRYPTO;
  ethereum_address_checksum(h, out, false, 0); /* writes "0x" + EIP-55 hex + NUL (43 bytes) */
  return WL_OK;
}

static wl_status_t sol_address(const uint8_t pub[32], char *out, size_t out_size) {
  size_t n = out_size;
  return b58enc(out, &n, pub, 32) ? WL_OK : WL_ERR_BUFFER;
}

wl_status_t wl_address(const uint8_t seed[64], wl_scheme_t s, wl_net_t net, uint32_t account,
                       uint32_t change, uint32_t index, char *out, size_t out_size) {
  const wl_scheme_info_t *info = wl_scheme_info(s);
  if (!info || !out || out_size == 0) return WL_ERR_ARG;
  out[0] = 0;

  if (s == WL_SCHEME_SOL_CLI_SEED) {
    uint32_t path[WL_MAX_PATH_DEPTH];
    size_t depth;
    wl_status_t st = wl_scheme_path(s, net, account, change, index, path, &depth);
    if (st != WL_OK) return st;
    uint8_t pub[32];
    ed25519_publickey(seed, pub); /* key = first 32 bytes of the BIP39 seed */
    return sol_address(pub, out, out_size);
  }

  HDNode node;
  wl_status_t st = derive(seed, s, net, account, change, index, &node);
  if (st == WL_OK) {
    switch (info->chain) {
      case WL_CHAIN_BTC: st = btc_address(&node, s, net, out, out_size); break;
      case WL_CHAIN_EVM: st = evm_address(&node, out, out_size); break;
      case WL_CHAIN_SOL: st = sol_address(node.public_key + 1, out, out_size); break;
      default: st = WL_ERR_ARG;
    }
  }
  wl_wipe(&node, sizeof(node));
  if (st != WL_OK) out[0] = 0;
  return st;
}

wl_status_t wl_account_key(const uint8_t seed[64], wl_scheme_t s, wl_net_t net, uint32_t account,
                           wl_account_key_t *out) {
  const wl_scheme_info_t *info = wl_scheme_info(s);
  if (!info || info->chain != WL_CHAIN_BTC || !out) return WL_ERR_ARG;
  uint32_t path[WL_MAX_PATH_DEPTH];
  size_t depth;
  wl_status_t st = wl_scheme_path(s, net, account, 0, 0, path, &depth);
  if (st != WL_OK) return st;
  HDNode node;
  st = WL_ERR_CRYPTO;
  if (hdnode_from_seed(seed, 64, SECP256K1_NAME, &node)) {
    out->master_fingerprint = hdnode_fingerprint(&node);
    int ok = 1;
    for (size_t i = 0; i < 3 && ok; i++) ok = hdnode_private_ckd(&node, path[i]); /* purpose'/coin'/account' */
    if (ok && hdnode_fill_public_key(&node) == 0) {
      memcpy(out->public_key, node.public_key, 33);
      memcpy(out->chain_code, node.chain_code, 32);
      st = WL_OK;
    }
  }
  wl_wipe(&node, sizeof(node));
  if (st != WL_OK) wl_wipe(out, sizeof(*out));
  return st;
}
