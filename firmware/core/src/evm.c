#include "wallet/evm.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "address.h" /* trezor-crypto: ethereum_address_checksum */
#include "secp256k1_recovery.h"
#include "sha3.h"
#include "wallet/keys.h"
#include "wallet/secure.h"

typedef struct {
  const uint8_t *p;
  size_t n;
  int list;
} rlp_t;

/* One canonical RLP item at p; *used = bytes consumed. */
static int rlp_item(const uint8_t *p, size_t n, rlp_t *it, size_t *used) {
  if (n < 1) return 0;
  uint8_t b = p[0];
  size_t hdr, len;
  if (b < 0x80) {
    it->p = p, it->n = 1, it->list = 0, *used = 1;
    return 1;
  }
  int list = b >= 0xc0;
  uint8_t base = list ? 0xc0 : 0x80;
  if (b - base <= 55) {
    hdr = 1;
    len = (size_t)(b - base);
  } else {
    size_t ll = (size_t)(b - base - 55);
    if (ll > 4 || n < 1 + ll || p[1] == 0) return 0;
    len = 0;
    for (size_t i = 0; i < ll; i++) len = len << 8 | p[1 + i];
    if (len <= 55) return 0;
    hdr = 1 + ll;
  }
  if (len > n - hdr) return 0;
  if (!list && len == 1 && p[hdr] < 0x80) return 0; /* single byte must encode itself */
  it->p = p + hdr, it->n = len, it->list = list, *used = hdr + len;
  return 1;
}

/* Unsigned integer field: string, <= max bytes, no leading zero. */
static int rlp_uint(const rlp_t *it, size_t max) { return !it->list && it->n <= max && (it->n == 0 || it->p[0] != 0); }

static uint64_t be_u64(const rlp_t *it) {
  uint64_t v = 0;
  for (size_t i = 0; i < it->n; i++) v = v << 8 | it->p[i];
  return v;
}

/* out (64 bytes BE) = a * b */
static void mul_be(const uint8_t *a, size_t an, const uint8_t *b, size_t bn, uint8_t out[64]) {
  uint32_t acc[64] = {0}; /* little-endian bytes with carry room */
  for (size_t i = 0; i < an; i++)
    for (size_t j = 0; j < bn; j++) acc[(an - 1 - i) + (bn - 1 - j)] += (uint32_t)a[i] * b[j];
  uint32_t carry = 0;
  for (size_t k = 0; k < 64; k++) {
    uint32_t v = acc[k] + carry;
    out[63 - k] = (uint8_t)v;
    carry = v >> 8;
  }
}

static const struct {
  uint64_t id;
  const char *name, *sym;
  int test;
} NETS[] = {
    {1, "Ethereum", "ETH", 0},          {11155111, "Sepolia", "ETH", 1},
    {8453, "Base", "ETH", 0},           {84532, "Base Sepolia", "ETH", 1},
    {56, "BNB Chain", "BNB", 0},        {97, "BNB testnet", "tBNB", 1},
    {137, "Polygon", "POL", 0},         {80002, "Polygon Amoy", "POL", 1},
    {42161, "Arbitrum One", "ETH", 0},  {421614, "Arbitrum Sepolia", "ETH", 1},
};

static void addr_str(const uint8_t a[20], char out[43]) { ethereum_address_checksum(a, out, false, 0); }

bool wl_evm_same_address(const char *a, const char *b) {
  if (strlen(a) != 42 || strlen(b) != 42 || a[0] != '0' || b[0] != '0') return false;
  for (size_t i = 0; i < 42; i++)
    if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
  return true;
}

#define FAIL(code, ...)               \
  do {                                \
    snprintf(err, errn, __VA_ARGS__); \
    return code;                      \
  } while (0)

wl_verdict_t wl_evm_analyze(const uint8_t *tx, size_t len, uint64_t chain_id, const char *from, uint32_t account,
                            bool blind, const wl_evm_hints_t *hints, wl_review_t *rv, char *err, size_t errn) {
  wl_review_init(rv);
  err[0] = 0;
  if (len < 2 || tx[0] != 0x02) FAIL(WL_VERDICT_UNSUPPORTED, "only EIP-1559 transactions");
  rlp_t list, f[9];
  size_t used;
  if (!rlp_item(tx + 1, len - 1, &list, &used) || !list.list || used != len - 1)
    FAIL(WL_VERDICT_INVALID, "bad transaction encoding");
  size_t off = 0, nf = 0;
  while (off < list.n) {
    rlp_t it;
    if (nf >= 9 || !rlp_item(list.p + off, list.n - off, &it, &used)) FAIL(WL_VERDICT_INVALID, "bad transaction fields");
    f[nf++] = it;
    off += used;
  }
  /* [chainId, nonce, maxPriorityFeePerGas, maxFeePerGas, gasLimit, to, value, data, accessList] */
  if (nf != 9) FAIL(WL_VERDICT_INVALID, "unsigned EIP-1559 tx must have 9 fields");
  if (!rlp_uint(&f[0], 8) || !rlp_uint(&f[1], 8) || !rlp_uint(&f[2], 32) || !rlp_uint(&f[3], 32) ||
      !rlp_uint(&f[4], 8) || !rlp_uint(&f[6], 32) || f[5].list || f[7].list || !f[8].list)
    FAIL(WL_VERDICT_INVALID, "bad transaction fields");
  if (be_u64(&f[0]) != chain_id) FAIL(WL_VERDICT_INVALID, "chainId mismatch");
  if (f[5].n == 0) FAIL(WL_VERDICT_UNSUPPORTED, "contract deployment not supported");
  if (f[5].n != 20) FAIL(WL_VERDICT_INVALID, "bad recipient");
  if (f[8].n && !blind) FAIL(WL_VERDICT_UNSUPPORTED, "access lists need blind signing");

  const char *net = NULL, *sym = "ETH";
  int known = 0;
  for (size_t i = 0; i < sizeof(NETS) / sizeof(NETS[0]); i++) {
    if (NETS[i].id == chain_id) net = NETS[i].name, sym = NETS[i].sym, known = 1;
  }
  char net_buf[32];
  if (!known) {
    snprintf(net_buf, sizeof(net_buf), "chain %llu", (unsigned long long)chain_id);
    net = net_buf;
  }

  char to[43];
  addr_str(f[5].p, to);
  wl_page_t *pg = wl_review_page(rv, "Network", known ? WL_TONE_NORMAL : WL_TONE_WARN);
  wl_page_line(pg, "%s", net);
  if (!known) wl_page_line(pg, "UNKNOWN network!");
  wl_page_line(pg, " ");
  wl_page_line(pg, "From account #%u", (unsigned)account);
  wl_page_wrap(pg, from, WL_ADDR_CHUNK);

  char amount[96];
  const rlp_t *data = &f[7];
  if (data->n == 0) {
    wl_format_units(f[6].p, f[6].n, 18, amount, sizeof(amount));
    pg = wl_review_page(rv, "Send", WL_TONE_NORMAL);
    wl_page_line(pg, "%s %s", amount, sym);
    wl_page_line(pg, "to");
    wl_page_wrap(pg, to, WL_ADDR_CHUNK);
    wl_review_add_recipient(rv, to);
  } else {
    const uint8_t *d = data->p;
    int is_transfer = data->n == 68 && !memcmp(d, "\xa9\x05\x9c\xbb", 4);
    int is_approve = data->n == 68 && !memcmp(d, "\x09\x5e\xa7\xb3", 4);
    int clean_addr = data->n == 68;
    for (size_t i = 0; i < 12 && clean_addr; i++) clean_addr = d[4 + i] == 0;
    if ((is_transfer || is_approve) && clean_addr && f[6].n == 0) {
      char who[43], units[96], tok[40];
      addr_str(d + 16, who);
      wl_format_units(d + 36, 32, 0, units, sizeof(units));
      snprintf(tok, sizeof(tok), "%s token contract", hints && hints->symbol[0] ? hints->symbol : "Unknown");
      if (is_transfer) {
        pg = wl_review_page(rv, "Token transfer", WL_TONE_NORMAL);
        wl_page_wrap(pg, units, 24);
        wl_page_line(pg, "base units");
        if (hints && hints->decimals >= 0 && hints->decimals <= 36) {
          char human[96];
          wl_format_units(d + 36, 32, (unsigned)hints->decimals, human, sizeof(human));
          wl_page_line(pg, "= %.12s %.6s", human, hints->symbol[0] ? hints->symbol : "tokens");
          wl_page_line(pg, "(decimals UNVERIFIED)");
        }
        wl_page_line(pg, "to");
        wl_page_wrap(pg, who, WL_ADDR_CHUNK);
        wl_review_add_recipient(rv, who);
      } else {
        int unlimited = 0;
        for (size_t i = 0; i < 16; i++) unlimited |= d[36 + i] != 0; /* >= 2^128 base units */
        pg = wl_review_page(rv, unlimited ? "UNLIMITED APPROVAL" : "Token approval",
                            unlimited ? WL_TONE_DANGER : WL_TONE_WARN);
        if (unlimited) {
          wl_page_line(pg, "Spender can take ALL of");
          wl_page_line(pg, "this token, forever:");
        } else {
          wl_page_line(pg, "Allow spending of");
          wl_page_wrap(pg, units, 24);
          wl_page_line(pg, "base units by:");
        }
        wl_page_wrap(pg, who, WL_ADDR_CHUNK);
      }
      pg = wl_review_page(rv, "Token contract", WL_TONE_NORMAL);
      wl_page_line(pg, "%s", tok);
      wl_page_wrap(pg, to, WL_ADDR_CHUNK);
      if (!hints || !hints->symbol[0]) wl_page_line(pg, "(name not verified)");
      else wl_page_line(pg, "(symbol from app,");
      if (hints && hints->symbol[0]) wl_page_line(pg, " not verified)");
    } else if (!blind) {
      FAIL(WL_VERDICT_UNSUPPORTED, "Cannot decode this contract call. Blind signing is disabled on the device.");
    } else {
      pg = wl_review_page(rv, "BLIND SIGNING", WL_TONE_DANGER);
      wl_page_line(pg, "Unknown contract call");
      wl_page_line(pg, "to");
      wl_page_wrap(pg, to, WL_ADDR_CHUNK);
      wl_page_line(pg, "data %02x%02x%02x%02x.. (%u B)", d[0], data->n > 1 ? d[1] : 0, data->n > 2 ? d[2] : 0,
                   data->n > 3 ? d[3] : 0, (unsigned)data->n);
      wl_page_line(pg, "You cannot verify what");
      wl_page_line(pg, "this does. Reject unless");
      wl_page_line(pg, "you are certain.");
      if (f[6].n) {
        wl_format_units(f[6].p, f[6].n, 18, amount, sizeof(amount));
        wl_page_line(pg, "Also sends %s %s", amount, sym);
      }
      wl_review_add_recipient(rv, to);
    }
  }

  uint8_t fee[64];
  mul_be(f[4].p, f[4].n, f[3].p, f[3].n, fee);
  for (size_t i = 0; i < 32; i++)
    if (fee[i]) FAIL(WL_VERDICT_INVALID, "absurd fee");
  wl_format_units(fee + 32, 32, 18, amount, sizeof(amount));
  pg = wl_review_page(rv, "Max fee", WL_TONE_NORMAL);
  wl_page_line(pg, "%s %s", amount, sym);
  wl_page_line(pg, "%s", net);
  if (rv->overflow) FAIL(WL_VERDICT_UNSUPPORTED, "too much to review on the device");
  return WL_VERDICT_OK;
}

wl_status_t wl_evm_sign(const uint8_t priv[32], const uint8_t *tx, size_t len, uint8_t r[32], uint8_t s[32],
                        uint8_t *y_parity) {
  const secp256k1_context *ctx = wl_secp_ctx();
  uint8_t hash[32], compact[64];
  int recid = 0;
  secp256k1_ecdsa_recoverable_signature sig;
  if (!ctx) return WL_ERR_CRYPTO;
  keccak_256(tx, len, hash);
  if (!secp256k1_ecdsa_sign_recoverable(ctx, &sig, hash, priv, NULL, NULL) ||
      !secp256k1_ecdsa_recoverable_signature_serialize_compact(ctx, compact, &recid, &sig))
    return WL_ERR_CRYPTO;
  memcpy(r, compact, 32);
  memcpy(s, compact + 32, 32);
  *y_parity = (uint8_t)recid;
  return WL_OK;
}
