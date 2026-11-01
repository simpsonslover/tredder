#include "wallet/btc.h"

#include <stdio.h>
#include <string.h>

#include "base58.h"
#include "ecdsa.h"
#include "hasher.h"
#include "rand.h"
#include "ripemd160.h"
#include "secp256k1_extrakeys.h"
#include "secp256k1_schnorrsig.h"
#include "segwit_addr.h"
#include "sha2.h"
#include "wallet/keys.h"
#include "wallet/secure.h"

#define H 0x80000000u

/* ---------------------------------------------------------------- byte reader */
typedef struct {
  const uint8_t *p;
  size_t n, off;
  int bad;
} rd_t;

static const uint8_t *take(rd_t *r, size_t k) {
  if (r->bad || k > r->n - r->off) {
    r->bad = 1;
    return NULL;
  }
  const uint8_t *q = r->p + r->off;
  r->off += k;
  return q;
}
static uint32_t u32le(rd_t *r) {
  const uint8_t *q = take(r, 4);
  return q ? (uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24 : 0;
}
static uint64_t u64le(rd_t *r) {
  uint64_t lo = u32le(r);
  return lo | (uint64_t)u32le(r) << 32;
}
static uint64_t varint(rd_t *r) {
  const uint8_t *q = take(r, 1);
  if (!q) return 0;
  if (*q < 0xFD) return *q;
  if (*q == 0xFD) {
    const uint8_t *b = take(r, 2);
    return b ? (uint64_t)b[0] | (uint64_t)b[1] << 8 : 0;
  }
  if (*q == 0xFE) return u32le(r);
  return u64le(r);
}

/* ---------------------------------------------------------------- hashing helpers */
static void put_u32(uint8_t *b, uint32_t v) {
  for (int i = 0; i < 4; i++) b[i] = (uint8_t)(v >> (8 * i));
}
static void put_u64(uint8_t *b, uint64_t v) {
  for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
}
static void sha_u32(SHA256_CTX *c, uint32_t v) {
  uint8_t b[4];
  put_u32(b, v);
  sha256_Update(c, b, 4);
}
static void sha_u64(SHA256_CTX *c, uint64_t v) {
  uint8_t b[8];
  put_u64(b, v);
  sha256_Update(c, b, 8);
}
static void sha_varint(SHA256_CTX *c, uint64_t v) {
  uint8_t b[9];
  size_t n;
  if (v < 0xFD) {
    b[0] = (uint8_t)v;
    n = 1;
  } else if (v <= 0xFFFF) {
    b[0] = 0xFD;
    b[1] = (uint8_t)v;
    b[2] = (uint8_t)(v >> 8);
    n = 3;
  } else {
    b[0] = 0xFE;
    put_u32(b + 1, (uint32_t)v);
    n = 5;
  }
  sha256_Update(c, b, n);
}
static void sha_final_double(SHA256_CTX *c, uint8_t out[32]) {
  sha256_Final(c, out);
  sha256_Raw(out, 32, out);
}
static void hash160(const uint8_t *d, size_t n, uint8_t out[20]) {
  uint8_t h[32];
  sha256_Raw(d, n, h);
  ripemd160(h, 32, out);
}

/* ---------------------------------------------------------------- scripts */
/* The scriptPubKey this wallet uses for `pub` under BIP `purpose`. */
static size_t own_script(uint8_t purpose, const uint8_t pub[33], uint8_t out[34]) {
  uint8_t h[20];
  switch (purpose) {
    case 44:
      hash160(pub, 33, h);
      out[0] = 0x76, out[1] = 0xa9, out[2] = 0x14;
      memcpy(out + 3, h, 20);
      out[23] = 0x88, out[24] = 0xac;
      return 25;
    case 49: {
      uint8_t redeem[22] = {0x00, 0x14};
      hash160(pub, 33, redeem + 2);
      hash160(redeem, 22, h);
      out[0] = 0xa9, out[1] = 0x14;
      memcpy(out + 2, h, 20);
      out[22] = 0x87;
      return 23;
    }
    case 84:
      hash160(pub, 33, h);
      out[0] = 0x00, out[1] = 0x14;
      memcpy(out + 2, h, 20);
      return 22;
    case 86:
      out[0] = 0x51, out[1] = 0x20;
      return wl_taproot_output_key(pub, out + 2) == WL_OK ? 34 : 0;
    default:
      return 0;
  }
}

wl_status_t wl_btc_script_address(const uint8_t *s, size_t n, wl_net_t net, char *out, size_t out_size) {
  const int test = net == WL_NET_TEST;
  const char *hrp = test ? "tb" : "bc";
  uint8_t pre[21];
  out[0] = 0;
  if (n == 25 && s[0] == 0x76 && s[1] == 0xa9 && s[2] == 0x14 && s[23] == 0x88 && s[24] == 0xac) {
    pre[0] = test ? 0x6F : 0x00;
    memcpy(pre + 1, s + 3, 20);
    return base58_encode_check(pre, 21, HASHER_SHA2D, out, (int)out_size) ? WL_OK : WL_ERR_BUFFER;
  }
  if (n == 23 && s[0] == 0xa9 && s[1] == 0x14 && s[22] == 0x87) {
    pre[0] = test ? 0xC4 : 0x05;
    memcpy(pre + 1, s + 2, 20);
    return base58_encode_check(pre, 21, HASHER_SHA2D, out, (int)out_size) ? WL_OK : WL_ERR_BUFFER;
  }
  if (out_size < 91) return WL_ERR_BUFFER;
  if ((n == 22 && s[0] == 0x00 && s[1] == 0x14) || (n == 34 && s[0] == 0x00 && s[1] == 0x20))
    return segwit_addr_encode(out, hrp, 0, s + 2, n - 2) ? WL_OK : WL_ERR_CRYPTO;
  if (n == 34 && s[0] == 0x51 && s[1] == 0x20) return segwit_addr_encode(out, hrp, 1, s + 2, 32) ? WL_OK : WL_ERR_CRYPTO;
  return WL_ERR_ARG;
}

/* ---------------------------------------------------------------- previous transaction (non-witness UTXO) */
/* Parse a full transaction (with or without witness), compute its txid, and
   return amount + script of output `vout`. */
static int prev_tx_output(const uint8_t *p, size_t n, const uint8_t want_txid[32], uint32_t vout, uint64_t *amount,
                          uint8_t *script, uint8_t *script_len) {
  rd_t r = {p, n, 0, 0};
  SHA256_CTX c;
  sha256_Init(&c);
  const uint8_t *ver = take(&r, 4);
  int segwit = 0;
  if (r.off + 2 <= n && p[r.off] == 0x00 && p[r.off + 1] == 0x01) {
    segwit = 1;
    take(&r, 2);
  }
  if (!ver) return 0;
  sha256_Update(&c, ver, 4);
  size_t body = r.off;
  uint64_t nin = varint(&r);
  for (uint64_t i = 0; i < nin && !r.bad; i++) {
    take(&r, 36);
    take(&r, (size_t)varint(&r));
    take(&r, 4);
  }
  uint64_t nout = varint(&r);
  int found = 0;
  for (uint64_t i = 0; i < nout && !r.bad; i++) {
    uint64_t v = u64le(&r);
    uint64_t sl = varint(&r);
    const uint8_t *sc = take(&r, (size_t)sl);
    if (sc && i == vout) {
      if (sl > 34) return 0; /* not one of our script types */
      *amount = v;
      memcpy(script, sc, (size_t)sl);
      *script_len = (uint8_t)sl;
      found = 1;
    }
  }
  if (r.bad) return 0;
  sha256_Update(&c, p + body, r.off - body);
  if (segwit) {
    for (uint64_t i = 0; i < nin && !r.bad; i++) {
      uint64_t items = varint(&r);
      for (uint64_t k = 0; k < items && !r.bad; k++) take(&r, (size_t)varint(&r));
    }
  }
  const uint8_t *lock = take(&r, 4);
  if (!lock || r.off != n) return 0;
  sha256_Update(&c, lock, 4);
  uint8_t id[32];
  sha_final_double(&c, id);
  return found && memcmp(id, want_txid, 32) == 0;
}

/* ---------------------------------------------------------------- path checks */
static int valid_path(const uint32_t *path, size_t depth, wl_net_t net, int want_change) {
  if (depth != 5) return 0;
  uint32_t purpose = path[0] & ~H;
  if (!(path[0] & H) || (purpose != 44 && purpose != 49 && purpose != 84 && purpose != 86)) return 0;
  if (path[1] != (H | (net == WL_NET_TEST ? 1u : 0u))) return 0;
  if (!(path[2] & H) || (path[3] & H) || (path[4] & H)) return 0;
  if (want_change >= 0 && path[3] != (uint32_t)want_change) return 0;
  return path[3] <= 1;
}

static int read_path(rd_t *v, uint32_t *fp, uint32_t path[5], size_t *depth) {
  size_t remain = v->n - v->off;
  if (remain < 4 || (remain - 4) % 4 || (remain - 4) / 4 > 5) return 0;
  const uint8_t *f = take(v, 4);
  *fp = (uint32_t)f[0] << 24 | (uint32_t)f[1] << 16 | (uint32_t)f[2] << 8 | f[3]; /* fingerprint is raw bytes */
  *depth = (remain - 4) / 4;
  for (size_t i = 0; i < *depth; i++) path[i] = u32le(v);
  return !v->bad;
}

/* ---------------------------------------------------------------- analysis */
#define FAIL(code, ...)                  \
  do {                                   \
    snprintf(err, errn, __VA_ARGS__);    \
    verdict = code;                      \
    goto done;                           \
  } while (0)

wl_verdict_t wl_btc_analyze(const uint8_t seed[64], wl_net_t net, const uint8_t *psbt, size_t len, wl_btc_tx_t *tx,
                            wl_review_t *rv, char *err, size_t errn) {
  wl_verdict_t verdict = WL_VERDICT_OK;
  rd_t r = {psbt, len, 0, 0};
  HDNode node;
  memset(&node, 0, sizeof(node));
  memset(tx, 0, sizeof(*tx));
  wl_review_init(rv);
  err[0] = 0;
  const uint32_t my_fp = wl_master_fingerprint(seed); /* big-endian, as PSBT stores the bytes */

  const uint8_t *magic = take(&r, 5);
  if (!magic || memcmp(magic, "psbt\xff", 5) != 0) FAIL(WL_VERDICT_INVALID, "not a PSBT");

  /* ---- global map */
  int have_tx = 0;
  for (;;) {
    uint64_t kl = varint(&r);
    if (r.bad) FAIL(WL_VERDICT_INVALID, "truncated PSBT");
    if (kl == 0) break;
    const uint8_t *key = take(&r, (size_t)kl);
    uint64_t vl = varint(&r);
    const uint8_t *val = take(&r, (size_t)vl);
    if (r.bad) FAIL(WL_VERDICT_INVALID, "truncated PSBT");
    if (key[0] == 0x00) {
      if (kl != 1 || have_tx) FAIL(WL_VERDICT_INVALID, "bad unsigned tx record");
      have_tx = 1;
      rd_t t = {val, (size_t)vl, 0, 0};
      tx->version = u32le(&t);
      if (t.off < t.n && val[t.off] == 0x00) FAIL(WL_VERDICT_INVALID, "unsigned tx has witness marker");
      uint64_t nin = varint(&t);
      if (nin == 0 || nin > WL_BTC_MAX_IN) FAIL(WL_VERDICT_UNSUPPORTED, "1..%u inputs supported", WL_BTC_MAX_IN);
      tx->nin = (size_t)nin;
      for (size_t i = 0; i < tx->nin; i++) {
        const uint8_t *id = take(&t, 32);
        if (id) memcpy(tx->in[i].txid, id, 32);
        tx->in[i].vout = u32le(&t);
        if (varint(&t) != 0) FAIL(WL_VERDICT_INVALID, "unsigned tx has scriptSig");
        tx->in[i].sequence = u32le(&t);
      }
      uint64_t nout = varint(&t);
      if (nout == 0 || nout > WL_BTC_MAX_OUT) FAIL(WL_VERDICT_UNSUPPORTED, "1..%u outputs supported", WL_BTC_MAX_OUT);
      tx->nout = (size_t)nout;
      for (size_t i = 0; i < tx->nout; i++) {
        tx->out[i].amount = u64le(&t);
        tx->out[i].script_len = (size_t)varint(&t);
        tx->out[i].script_off = (size_t)(val - psbt) + t.off;
        take(&t, tx->out[i].script_len);
      }
      tx->locktime = u32le(&t);
      if (t.bad || t.off != t.n) FAIL(WL_VERDICT_INVALID, "malformed unsigned tx");
    } else if (key[0] == 0xFB) {
      rd_t v = {val, (size_t)vl, 0, 0};
      if (u32le(&v) != 0) FAIL(WL_VERDICT_UNSUPPORTED, "only PSBT version 0");
    }
  }
  if (!have_tx) FAIL(WL_VERDICT_INVALID, "PSBT has no unsigned tx");

  /* ---- inputs: every one must be ours */
  uint64_t in_sum = 0;
  for (size_t i = 0; i < tx->nin; i++) {
    wl_btc_in_t *in = &tx->in[i];
    in->map_start = r.off;
    int have_wu = 0, have_nwu = 0, have_der = 0, have_redeem = 0;
    uint64_t wu_amount = 0, nwu_amount = 0;
    uint8_t wu_script[34], nwu_script[34], redeem[34], listed[33];
    uint8_t wu_len = 0, nwu_len = 0, redeem_len = 0, listed_len = 0;
    uint32_t path[5];
    size_t depth = 0;
    for (;;) {
      size_t at = r.off;
      uint64_t kl = varint(&r);
      if (r.bad) FAIL(WL_VERDICT_INVALID, "truncated input %u", (unsigned)i);
      if (kl == 0) {
        in->map_end = at;
        break;
      }
      const uint8_t *key = take(&r, (size_t)kl);
      uint64_t vl = varint(&r);
      const uint8_t *val = take(&r, (size_t)vl);
      if (r.bad) FAIL(WL_VERDICT_INVALID, "truncated input %u", (unsigned)i);
      rd_t v = {val, (size_t)vl, 0, 0};
      switch (key[0]) {
        case 0x00: /* NON_WITNESS_UTXO */
          if (kl != 1 || have_nwu) FAIL(WL_VERDICT_INVALID, "input %u: bad previous tx", (unsigned)i);
          if (!prev_tx_output(val, (size_t)vl, in->txid, in->vout, &nwu_amount, nwu_script, &nwu_len))
            FAIL(WL_VERDICT_INVALID, "input %u: previous tx does not match its txid", (unsigned)i);
          have_nwu = 1;
          break;
        case 0x01: { /* WITNESS_UTXO */
          if (kl != 1 || have_wu) FAIL(WL_VERDICT_INVALID, "input %u: bad witness utxo", (unsigned)i);
          wu_amount = u64le(&v);
          uint64_t sl = varint(&v);
          const uint8_t *sc = take(&v, (size_t)sl);
          if (v.bad || v.off != v.n || sl > 34) FAIL(WL_VERDICT_INVALID, "input %u: bad witness utxo", (unsigned)i);
          memcpy(wu_script, sc, (size_t)sl);
          wu_len = (uint8_t)sl;
          have_wu = 1;
          break;
        }
        case 0x02:
        case 0x13:
          FAIL(WL_VERDICT_INVALID, "input %u is already signed", (unsigned)i);
        case 0x07:
        case 0x08:
          FAIL(WL_VERDICT_INVALID, "input %u is already finalized", (unsigned)i);
        case 0x03: { /* SIGHASH_TYPE */
          uint32_t sh = u32le(&v);
          if (kl != 1 || v.bad || v.off != v.n || !(sh == 1 || sh == 0))
            FAIL(WL_VERDICT_UNSUPPORTED, "input %u: only SIGHASH_ALL", (unsigned)i);
          break;
        }
        case 0x04: /* REDEEM_SCRIPT */
          if (kl != 1 || vl > 34 || have_redeem) FAIL(WL_VERDICT_INVALID, "input %u: bad redeem script", (unsigned)i);
          memcpy(redeem, val, (size_t)vl);
          redeem_len = (uint8_t)vl;
          have_redeem = 1;
          break;
        case 0x06:   /* BIP32_DERIVATION: key = 0x06 || pubkey33 */
        case 0x16: { /* TAP_BIP32_DERIVATION: key = 0x16 || xonly32 */
          int tap = key[0] == 0x16;
          if (kl != (tap ? 33u : 34u)) FAIL(WL_VERDICT_INVALID, "input %u: bad key origin", (unsigned)i);
          if (tap) {
            uint64_t nh = varint(&v);
            if (nh != 0) FAIL(WL_VERDICT_UNSUPPORTED, "input %u: taproot script path not supported", (unsigned)i);
          }
          uint32_t fp, p[5];
          size_t d;
          if (!read_path(&v, &fp, p, &d)) FAIL(WL_VERDICT_INVALID, "input %u: bad key origin", (unsigned)i);
          if (fp != my_fp) break; /* someone else's key (multisig etc.): not used */
          if (have_der) FAIL(WL_VERDICT_INVALID, "input %u: several keys of this wallet", (unsigned)i);
          have_der = 1;
          memcpy(path, p, sizeof(p));
          depth = d;
          listed_len = (uint8_t)(kl - 1);
          memcpy(listed, key + 1, listed_len);
          break;
        }
        default:
          break;
      }
    }
    if (!have_der) FAIL(WL_VERDICT_INVALID, "input %u does not belong to this wallet", (unsigned)i);
    if (!valid_path(path, depth, net, -1)) FAIL(WL_VERDICT_INVALID, "input %u: unexpected key path", (unsigned)i);
    in->purpose = (uint8_t)(path[0] & ~H);
    memcpy(in->path, path, sizeof(path));
    if (wl_node_at(seed, path, 5, &node) != WL_OK) FAIL(WL_VERDICT_INVALID, "input %u: derivation failed", (unsigned)i);
    const int tap = in->purpose == 86;
    if ((tap && (listed_len != 32 || memcmp(listed, node.public_key + 1, 32) != 0)) ||
        (!tap && (listed_len != 33 || memcmp(listed, node.public_key, 33) != 0)))
      FAIL(WL_VERDICT_INVALID, "input %u: key origin does not match its public key", (unsigned)i);
    in->script_len = (uint8_t)own_script(in->purpose, node.public_key, in->script);
    wl_wipe(&node, sizeof(node));

    if (in->purpose == 44 && !have_nwu)
      FAIL(WL_VERDICT_INVALID, "input %u: legacy input needs the previous transaction", (unsigned)i);
    if (!have_nwu && !have_wu) FAIL(WL_VERDICT_INVALID, "input %u: amount unknown (can't compute fee)", (unsigned)i);
    const uint8_t *spk = have_nwu ? nwu_script : wu_script;
    uint8_t spk_len = have_nwu ? nwu_len : wu_len;
    in->amount = have_nwu ? nwu_amount : wu_amount;
    if (have_nwu && have_wu && (wu_amount != nwu_amount || wu_len != nwu_len || memcmp(wu_script, nwu_script, wu_len)))
      FAIL(WL_VERDICT_INVALID, "input %u: witness utxo contradicts previous tx", (unsigned)i);
    if (spk_len != in->script_len || memcmp(spk, in->script, spk_len) != 0)
      FAIL(WL_VERDICT_INVALID, "input %u: spent script is not this key's", (unsigned)i);
    if (have_redeem && (in->purpose != 49 || redeem_len != 22 || redeem[0] != 0x00 || redeem[1] != 0x14))
      FAIL(WL_VERDICT_INVALID, "input %u: unexpected redeem script", (unsigned)i);
    if (in_sum + in->amount < in_sum) FAIL(WL_VERDICT_INVALID, "amount overflow");
    in_sum += in->amount;
  }

  /* ---- outputs */
  tx->outputs_start = r.off;
  uint64_t out_sum = 0, change_sum = 0;
  int sends = 0;
  for (size_t i = 0; i < tx->nout; i++) {
    const wl_btc_out_t *o = &tx->out[i];
    int is_change = 0;
    for (;;) {
      uint64_t kl = varint(&r);
      if (r.bad) FAIL(WL_VERDICT_INVALID, "truncated output %u", (unsigned)i);
      if (kl == 0) break;
      const uint8_t *key = take(&r, (size_t)kl);
      uint64_t vl = varint(&r);
      const uint8_t *val = take(&r, (size_t)vl);
      if (r.bad) FAIL(WL_VERDICT_INVALID, "truncated output %u", (unsigned)i);
      if ((key[0] == 0x02 && kl == 34) || (key[0] == 0x07 && kl == 33)) {
        rd_t v = {val, (size_t)vl, 0, 0};
        if (key[0] == 0x07 && varint(&v) != 0) continue;
        uint32_t fp, p[5];
        size_t d;
        if (!read_path(&v, &fp, p, &d) || fp != my_fp || !valid_path(p, d, net, 1)) continue;
        /* Ours only if the device derives exactly this script for this path. */
        if (wl_node_at(seed, p, 5, &node) != WL_OK) continue;
        uint8_t sc[34];
        size_t sl = own_script((uint8_t)(p[0] & ~H), node.public_key, sc);
        wl_wipe(&node, sizeof(node));
        if (sl && sl == o->script_len && memcmp(sc, psbt + o->script_off, sl) == 0) is_change = 1;
      }
    }
    if (out_sum + o->amount < out_sum) FAIL(WL_VERDICT_INVALID, "amount overflow");
    out_sum += o->amount;
    char amount[40];
    wl_format_u64(o->amount, 8, amount, sizeof(amount));
    if (is_change) {
      change_sum += o->amount;
      continue;
    }
    const uint8_t *sc = psbt + o->script_off;
    if (o->script_len >= 1 && sc[0] == 0x6a) { /* OP_RETURN: data, not a payment */
      wl_page_t *pg = wl_review_page(rv, "Data output", WL_TONE_WARN);
      wl_page_line(pg, "OP_RETURN (%u bytes)", (unsigned)o->script_len);
      wl_page_line(pg, "burns %s BTC", amount);
      continue;
    }
    char addr[WL_ADDRESS_MAX_LEN];
    if (wl_btc_script_address(sc, o->script_len, net, addr, sizeof(addr)) != WL_OK)
      FAIL(WL_VERDICT_UNSUPPORTED, "output %u: non-standard script", (unsigned)i);
    char title[24];
    snprintf(title, sizeof(title), "Send %d", ++sends);
    wl_page_t *pg = wl_review_page(rv, title, WL_TONE_NORMAL);
    wl_page_line(pg, "%s BTC", amount);
    wl_page_line(pg, "to");
    wl_page_wrap(pg, addr, WL_ADDR_CHUNK);
    wl_review_add_recipient(rv, addr);
  }
  if (r.off != len) FAIL(WL_VERDICT_INVALID, "trailing bytes after PSBT");
  if (out_sum > in_sum) FAIL(WL_VERDICT_INVALID, "outputs exceed inputs");

  uint64_t fee = in_sum - out_sum, sent = out_sum - change_sum;
  int high = sent > 0 && fee > sent / 10;
  char s_fee[40], s_change[40];
  wl_format_u64(fee, 8, s_fee, sizeof(s_fee));
  wl_page_t *pg = wl_review_page(rv, "Fee", high ? WL_TONE_WARN : WL_TONE_NORMAL);
  wl_page_line(pg, "%s BTC", s_fee);
  if (change_sum) {
    wl_format_u64(change_sum, 8, s_change, sizeof(s_change));
    wl_page_line(pg, " ");
    wl_page_line(pg, "Change back to you:");
    wl_page_line(pg, "%s BTC", s_change);
    wl_page_line(pg, "(verified by device)");
  }
  wl_page_line(pg, " ");
  wl_page_line(pg, net == WL_NET_TEST ? "Bitcoin TESTNET" : "Bitcoin MAINNET");
  if (high) {
    pg = wl_review_page(rv, "High fee!", WL_TONE_WARN);
    wl_page_line(pg, "The fee is more than 10%%");
    wl_page_line(pg, "of the amount sent.");
    wl_page_line(pg, "Reject if unexpected.");
  }
  if (rv->overflow) FAIL(WL_VERDICT_UNSUPPORTED, "transaction too large to review on the device");

done:
  wl_wipe(&node, sizeof(node));
  return verdict;
}

/* ---------------------------------------------------------------- signing */
typedef struct {
  uint8_t *b;
  size_t cap, len;
  int bad;
} wr_t;
static void wr(wr_t *w, const void *d, size_t n) {
  if (w->bad || n > w->cap - w->len) {
    w->bad = 1;
    return;
  }
  memcpy(w->b + w->len, d, n);
  w->len += n;
}
static void wr_varint(wr_t *w, uint64_t v) {
  uint8_t b[5];
  if (v < 0xFD) {
    b[0] = (uint8_t)v;
    wr(w, b, 1);
  } else if (v <= 0xFFFF) {
    b[0] = 0xFD, b[1] = (uint8_t)v, b[2] = (uint8_t)(v >> 8);
    wr(w, b, 3);
  } else {
    b[0] = 0xFE;
    put_u32(b + 1, (uint32_t)v);
    wr(w, b, 5);
  }
}

static void sha_outputs(SHA256_CTX *c, const wl_btc_tx_t *tx, const uint8_t *psbt) {
  for (size_t i = 0; i < tx->nout; i++) {
    sha_u64(c, tx->out[i].amount);
    sha_varint(c, tx->out[i].script_len);
    sha256_Update(c, psbt + tx->out[i].script_off, tx->out[i].script_len);
  }
}

static void sighash_legacy(const wl_btc_tx_t *tx, const uint8_t *psbt, size_t idx, uint8_t out[32]) {
  SHA256_CTX c;
  sha256_Init(&c);
  sha_u32(&c, tx->version);
  sha_varint(&c, tx->nin);
  for (size_t i = 0; i < tx->nin; i++) {
    sha256_Update(&c, tx->in[i].txid, 32);
    sha_u32(&c, tx->in[i].vout);
    if (i == idx) {
      sha_varint(&c, tx->in[i].script_len);
      sha256_Update(&c, tx->in[i].script, tx->in[i].script_len);
    } else {
      sha_varint(&c, 0);
    }
    sha_u32(&c, tx->in[i].sequence);
  }
  sha_varint(&c, tx->nout);
  sha_outputs(&c, tx, psbt);
  sha_u32(&c, tx->locktime);
  sha_u32(&c, 1); /* SIGHASH_ALL */
  sha_final_double(&c, out);
}

static void sighash_bip143(const wl_btc_tx_t *tx, const uint8_t *psbt, size_t idx, const uint8_t pub[33], uint8_t out[32]) {
  uint8_t prevouts[32], sequences[32], outputs[32], h[20];
  SHA256_CTX c;
  sha256_Init(&c);
  for (size_t i = 0; i < tx->nin; i++) {
    sha256_Update(&c, tx->in[i].txid, 32);
    sha_u32(&c, tx->in[i].vout);
  }
  sha_final_double(&c, prevouts);
  sha256_Init(&c);
  for (size_t i = 0; i < tx->nin; i++) sha_u32(&c, tx->in[i].sequence);
  sha_final_double(&c, sequences);
  sha256_Init(&c);
  sha_outputs(&c, tx, psbt);
  sha_final_double(&c, outputs);

  hash160(pub, 33, h);
  uint8_t script_code[26] = {0x19, 0x76, 0xa9, 0x14};
  memcpy(script_code + 4, h, 20);
  script_code[24] = 0x88, script_code[25] = 0xac;

  const wl_btc_in_t *in = &tx->in[idx];
  sha256_Init(&c);
  sha_u32(&c, tx->version);
  sha256_Update(&c, prevouts, 32);
  sha256_Update(&c, sequences, 32);
  sha256_Update(&c, in->txid, 32);
  sha_u32(&c, in->vout);
  sha256_Update(&c, script_code, sizeof(script_code));
  sha_u64(&c, in->amount);
  sha_u32(&c, in->sequence);
  sha256_Update(&c, outputs, 32);
  sha_u32(&c, tx->locktime);
  sha_u32(&c, 1);
  sha_final_double(&c, out);
}

/* BIP341 key-path signature hash, SIGHASH_DEFAULT, no annex. */
static void sighash_taproot(const wl_btc_tx_t *tx, const uint8_t *psbt, size_t idx, uint8_t out[32]) {
  uint8_t sha_prevouts[32], sha_amounts[32], sha_scripts[32], sha_seq[32], sha_outs[32];
  SHA256_CTX c;
  sha256_Init(&c);
  for (size_t i = 0; i < tx->nin; i++) {
    sha256_Update(&c, tx->in[i].txid, 32);
    sha_u32(&c, tx->in[i].vout);
  }
  sha256_Final(&c, sha_prevouts);
  sha256_Init(&c);
  for (size_t i = 0; i < tx->nin; i++) sha_u64(&c, tx->in[i].amount);
  sha256_Final(&c, sha_amounts);
  sha256_Init(&c);
  for (size_t i = 0; i < tx->nin; i++) {
    sha_varint(&c, tx->in[i].script_len);
    sha256_Update(&c, tx->in[i].script, tx->in[i].script_len);
  }
  sha256_Final(&c, sha_scripts);
  sha256_Init(&c);
  for (size_t i = 0; i < tx->nin; i++) sha_u32(&c, tx->in[i].sequence);
  sha256_Final(&c, sha_seq);
  sha256_Init(&c);
  sha_outputs(&c, tx, psbt);
  sha256_Final(&c, sha_outs);

  /* tagged hash: SHA256(SHA256(tag) || SHA256(tag) || 0x00 epoch || SigMsg) */
  uint8_t tag[32];
  sha256_Raw((const uint8_t *)"TapSighash", 10, tag);
  sha256_Init(&c);
  sha256_Update(&c, tag, 32);
  sha256_Update(&c, tag, 32);
  const uint8_t epoch_and_type[2] = {0x00, 0x00};
  sha256_Update(&c, epoch_and_type, 2);
  sha_u32(&c, tx->version);
  sha_u32(&c, tx->locktime);
  sha256_Update(&c, sha_prevouts, 32);
  sha256_Update(&c, sha_amounts, 32);
  sha256_Update(&c, sha_scripts, 32);
  sha256_Update(&c, sha_seq, 32);
  sha256_Update(&c, sha_outs, 32);
  const uint8_t spend_type = 0x00;
  sha256_Update(&c, &spend_type, 1);
  sha_u32(&c, (uint32_t)idx);
  sha256_Final(&c, out);
}

wl_status_t wl_btc_sign(const uint8_t seed[64], const wl_btc_tx_t *tx, const uint8_t *psbt, size_t len, uint8_t *out,
                        size_t cap, size_t *outlen) {
  const secp256k1_context *ctx = wl_secp_ctx();
  if (!ctx || tx->nin == 0) return WL_ERR_CRYPTO;
  wr_t w = {out, cap, 0, 0};
  HDNode node;
  wl_status_t st = WL_OK;
  wr(&w, psbt, tx->in[0].map_start);
  for (size_t i = 0; i < tx->nin && st == WL_OK; i++) {
    const wl_btc_in_t *in = &tx->in[i];
    wr(&w, psbt + in->map_start, in->map_end - in->map_start);
    if (wl_node_at(seed, in->path, 5, &node) != WL_OK) {
      st = WL_ERR_CRYPTO;
      break;
    }
    uint8_t digest[32];
    if (in->purpose == 86) {
      uint8_t tweak[32], aux[32], sig[64];
      secp256k1_keypair kp;
      random_buffer(aux, sizeof(aux));
      sighash_taproot(tx, psbt, i, digest);
      if (wl_taproot_tweak(node.public_key, tweak) != WL_OK || !secp256k1_keypair_create(ctx, &kp, node.private_key) ||
          !secp256k1_keypair_xonly_tweak_add(ctx, &kp, tweak) || !secp256k1_schnorrsig_sign32(ctx, sig, digest, &kp, aux)) {
        st = WL_ERR_CRYPTO;
      } else {
        const uint8_t k = 0x13; /* TAP_KEY_SIG */
        wr_varint(&w, 1);
        wr(&w, &k, 1);
        wr_varint(&w, 64);
        wr(&w, sig, 64);
      }
      wl_wipe(&kp, sizeof(kp));
    } else {
      if (in->purpose == 44) sighash_legacy(tx, psbt, i, digest);
      else sighash_bip143(tx, psbt, i, node.public_key, digest);
      secp256k1_ecdsa_signature sig;
      uint8_t der[73];
      size_t der_len = sizeof(der) - 1;
      if (!secp256k1_ecdsa_sign(ctx, &sig, digest, node.private_key, NULL, NULL) ||
          !secp256k1_ecdsa_signature_serialize_der(ctx, der, &der_len, &sig)) {
        st = WL_ERR_CRYPTO;
      } else {
        der[der_len++] = 0x01; /* SIGHASH_ALL */
        uint8_t key[34] = {0x02}; /* PARTIAL_SIG || pubkey */
        memcpy(key + 1, node.public_key, 33);
        wr_varint(&w, sizeof(key));
        wr(&w, key, sizeof(key));
        wr_varint(&w, der_len);
        wr(&w, der, der_len);
      }
    }
    wl_wipe(&node, sizeof(node));
    const uint8_t end = 0x00;
    wr(&w, &end, 1);
  }
  wr(&w, psbt + tx->outputs_start, len - tx->outputs_start);
  if (st == WL_OK && w.bad) st = WL_ERR_BUFFER;
  *outlen = st == WL_OK ? w.len : 0;
  return st;
}
