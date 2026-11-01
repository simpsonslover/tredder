#include "wallet/keys.h"

#include <string.h>

#include "curves.h"
#include "ed25519-donna/ed25519.h"
#include "rand.h"
#include "secp256k1_extrakeys.h"
#include "secp256k1_preallocated.h"
#include "wallet/secure.h"

const struct secp256k1_context_struct *wl_secp_ctx(void) {
  static uint64_t mem[1024]; /* 8 KiB, ample for a v0.8 context (checked below) */
  static secp256k1_context *ctx;
  if (!ctx) {
    size_t need = secp256k1_context_preallocated_size(SECP256K1_CONTEXT_NONE);
    if (need > sizeof(mem)) return NULL;
    ctx = secp256k1_context_preallocated_create(mem, SECP256K1_CONTEXT_NONE);
    uint8_t blind[32];
    random_buffer(blind, sizeof(blind)); /* side-channel blinding of the signing tables */
    if (!secp256k1_context_randomize(ctx, blind)) ctx = NULL;
    wl_wipe(blind, sizeof(blind));
  }
  return ctx;
}

wl_status_t wl_node_at(const uint8_t seed[64], const uint32_t *path, size_t depth, HDNode *out) {
  if (!hdnode_from_seed(seed, 64, SECP256K1_NAME, out)) return WL_ERR_CRYPTO;
  for (size_t i = 0; i < depth; i++) {
    if (!hdnode_private_ckd(out, path[i])) {
      wl_wipe(out, sizeof(*out));
      return WL_ERR_CRYPTO;
    }
  }
  if (hdnode_fill_public_key(out) != 0) { /* NB: 0 = success here */
    wl_wipe(out, sizeof(*out));
    return WL_ERR_CRYPTO;
  }
  return WL_OK;
}

uint32_t wl_master_fingerprint(const uint8_t seed[64]) {
  HDNode n;
  uint32_t fp = 0;
  if (hdnode_from_seed(seed, 64, SECP256K1_NAME, &n)) fp = hdnode_fingerprint(&n);
  wl_wipe(&n, sizeof(n));
  return fp;
}

wl_status_t wl_scheme_privkey(const uint8_t seed[64], wl_scheme_t s, wl_net_t net, uint32_t account,
                              uint32_t change, uint32_t index, uint8_t priv[32]) {
  const wl_scheme_info_t *info = wl_scheme_info(s);
  uint32_t path[WL_MAX_PATH_DEPTH];
  size_t depth;
  if (!info) return WL_ERR_ARG;
  wl_status_t st = wl_scheme_path(s, net, account, change, index, path, &depth);
  if (st != WL_OK) return st;
  if (s == WL_SCHEME_SOL_CLI_SEED) {
    memcpy(priv, seed, 32);
    return WL_OK;
  }
  HDNode n;
  const char *curve = info->curve == WL_CURVE_ED25519 ? ED25519_NAME : SECP256K1_NAME;
  st = WL_ERR_CRYPTO;
  if (hdnode_from_seed(seed, 64, curve, &n)) {
    int ok = 1;
    for (size_t i = 0; i < depth && ok; i++) ok = hdnode_private_ckd(&n, path[i]);
    if (ok) {
      memcpy(priv, n.private_key, 32);
      st = WL_OK;
    }
  }
  wl_wipe(&n, sizeof(n));
  return st;
}

wl_status_t wl_sol_pubkey(const uint8_t seed[64], wl_scheme_t s, uint32_t account, uint8_t pub[32]) {
  if (!wl_scheme_info(s) || wl_scheme_info(s)->chain != WL_CHAIN_SOL) return WL_ERR_ARG;
  uint8_t sk[32];
  wl_status_t st = wl_scheme_privkey(seed, s, WL_NET_MAIN, account, 0, 0, sk);
  if (st == WL_OK) ed25519_publickey(sk, pub);
  wl_wipe(sk, sizeof(sk));
  return st;
}

wl_status_t wl_taproot_tweak(const uint8_t pub33[33], uint8_t tweak[32]) {
  const secp256k1_context *ctx = secp256k1_context_static;
  secp256k1_pubkey pk;
  secp256k1_xonly_pubkey xonly;
  uint8_t px[32];
  static const unsigned char TAG[] = "TapTweak";
  if (secp256k1_ec_pubkey_parse(ctx, &pk, pub33, 33) && secp256k1_xonly_pubkey_from_pubkey(ctx, &xonly, NULL, &pk) &&
      secp256k1_xonly_pubkey_serialize(ctx, px, &xonly) &&
      secp256k1_tagged_sha256(ctx, tweak, TAG, sizeof(TAG) - 1, px, 32)) {
    return WL_OK;
  }
  return WL_ERR_CRYPTO;
}

wl_status_t wl_taproot_output_key(const uint8_t pub33[33], uint8_t out_x[32]) {
  const secp256k1_context *ctx = secp256k1_context_static;
  secp256k1_pubkey pk, tweaked;
  secp256k1_xonly_pubkey xonly;
  uint8_t tweak[32];
  if (wl_taproot_tweak(pub33, tweak) == WL_OK && secp256k1_ec_pubkey_parse(ctx, &pk, pub33, 33) &&
      secp256k1_xonly_pubkey_from_pubkey(ctx, &xonly, NULL, &pk) &&
      secp256k1_xonly_pubkey_tweak_add(ctx, &tweaked, &xonly, tweak) &&
      secp256k1_xonly_pubkey_from_pubkey(ctx, &xonly, NULL, &tweaked) &&
      secp256k1_xonly_pubkey_serialize(ctx, out_x, &xonly)) {
    return WL_OK;
  }
  return WL_ERR_CRYPTO;
}

int wl_scheme_by_name(const char *name) {
  for (int s = 0; s < WL_SCHEME_COUNT; s++) {
    if (strcmp(wl_scheme_info((wl_scheme_t)s)->name, name) == 0) return s;
  }
  return -1;
}
