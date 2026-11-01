#include "wallet/sol.h"

#include <stdio.h>
#include <string.h>

#include "base58.h"
#include "ed25519-donna/ed25519.h"

#define MAX_KEYS 64

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
static uint8_t u8(rd_t *r) {
  const uint8_t *q = take(r, 1);
  return q ? *q : 0;
}
/* Solana "compact-u16" (shortvec) */
static size_t shortvec(rd_t *r) {
  size_t v = 0;
  for (int i = 0; i < 3; i++) {
    uint8_t b = u8(r);
    v |= (size_t)(b & 0x7f) << (7 * i);
    if (!(b & 0x80)) return v;
  }
  r->bad = 1;
  return 0;
}
static uint64_t le64(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
  return v;
}

void wl_sol_address(const uint8_t pub[32], char *out, size_t out_size) {
  size_t n = out_size;
  if (!b58enc(out, &n, pub, 32)) out[0] = 0;
}

/* Program ids (base58 decoded once at first use). */
static const char *const PROGRAM_B58[] = {
    "11111111111111111111111111111111",            /* System */
    "TokenkegQfeZyiNwAJbNbGKPFXCWuBvf9Ss623VQ5DA", /* SPL Token */
    "TokenzQdBNbLqP5VEhdkAS6EPFLC1PHnBqCXEpPxuEb", /* Token-2022 */
    "ATokenGPvbdGVxr1b2hvZbsiqW5xWH25efTNsLJA8knL", /* Associated Token Account */
    "ComputeBudget111111111111111111111111111111",  /* Compute Budget */
};
enum { P_SYSTEM, P_TOKEN, P_TOKEN22, P_ATA, P_COMPUTE, P_COUNT, P_UNKNOWN = P_COUNT };

static int program_of(const uint8_t key[32]) {
  static uint8_t ids[P_COUNT][32];
  static int ready;
  if (!ready) {
    for (int i = 0; i < P_COUNT; i++) {
      size_t n = 32;
      uint8_t tmp[32];
      if (b58tobin(tmp, &n, PROGRAM_B58[i]) && n == 32) memcpy(ids[i], tmp, 32);
      else if (i == P_SYSTEM) memset(ids[i], 0, 32); /* all-zero key */
    }
    ready = 1;
  }
  for (int i = 0; i < P_COUNT; i++)
    if (!memcmp(ids[i], key, 32)) return i;
  return P_UNKNOWN;
}

#define FAIL(code, ...)               \
  do {                                \
    snprintf(err, errn, __VA_ARGS__); \
    return code;                      \
  } while (0)

wl_verdict_t wl_sol_analyze(const uint8_t *msg, size_t len, const uint8_t me[32], bool testnet, uint32_t account,
                            bool blind, wl_review_t *rv, char *err, size_t errn) {
  wl_review_init(rv);
  err[0] = 0;
  rd_t r = {msg, len, 0, 0};
  int v0 = 0;
  if (len && (msg[0] & 0x80)) {
    if ((msg[0] & 0x7f) != 0) FAIL(WL_VERDICT_UNSUPPORTED, "unknown message version");
    v0 = 1;
    take(&r, 1);
  }
  uint8_t n_sig = u8(&r);
  u8(&r); /* readonly signed */
  u8(&r); /* readonly unsigned */
  size_t nkeys = shortvec(&r);
  if (r.bad || nkeys == 0 || nkeys > MAX_KEYS) FAIL(WL_VERDICT_INVALID, "bad message header");
  const uint8_t *keys = take(&r, 32 * nkeys);
  take(&r, 32); /* recent blockhash */
  if (r.bad) FAIL(WL_VERDICT_INVALID, "truncated message");
  if (memcmp(keys, me, 32) != 0) FAIL(WL_VERDICT_INVALID, "fee payer is not this account");
  if (n_sig != 1) FAIL(WL_VERDICT_UNSUPPORTED, "only single-signer transactions");

  char me_s[48];
  wl_sol_address(me, me_s, sizeof(me_s));
  wl_page_t *pg = wl_review_page(rv, "Network", WL_TONE_NORMAL);
  wl_page_line(pg, testnet ? "Solana devnet" : "Solana MAINNET");
  wl_page_line(pg, " ");
  wl_page_line(pg, "Account #%u", (unsigned)account);
  wl_page_wrap(pg, me_s, WL_ADDR_CHUNK);

  size_t nix = shortvec(&r);
  if (r.bad || nix > 16) FAIL(WL_VERDICT_UNSUPPORTED, "too many instructions");
  /* First pass is the instructions; the lookup tables follow (v0), so remember spans. */
  struct {
    uint8_t prog;
    const uint8_t *acc;
    size_t nacc;
    const uint8_t *data;
    size_t ndata;
  } ix[16];
  for (size_t i = 0; i < nix; i++) {
    ix[i].prog = u8(&r);
    ix[i].nacc = shortvec(&r);
    ix[i].acc = take(&r, ix[i].nacc);
    ix[i].ndata = shortvec(&r);
    ix[i].data = take(&r, ix[i].ndata);
    if (r.bad) FAIL(WL_VERDICT_INVALID, "truncated instruction");
  }
  size_t nlookups = 0;
  if (v0) {
    nlookups = shortvec(&r);
    for (size_t i = 0; i < nlookups && !r.bad; i++) {
      take(&r, 32);
      take(&r, shortvec(&r));
      take(&r, shortvec(&r));
    }
  }
  if (r.bad || r.off != len) FAIL(WL_VERDICT_INVALID, "malformed message");
  if (nlookups && !blind) FAIL(WL_VERDICT_UNSUPPORTED, "address lookup tables need blind signing");

  for (size_t i = 0; i < nix; i++) {
    if (ix[i].prog >= nkeys) FAIL(WL_VERDICT_INVALID, "bad program index");
    int all_static = 1;
    for (size_t k = 0; k < ix[i].nacc; k++) all_static &= ix[i].acc[k] < nkeys;
    int prog = all_static ? program_of(keys + 32 * ix[i].prog) : P_UNKNOWN;
    const uint8_t *d = ix[i].data;
    size_t dn = ix[i].ndata;
#define ACC(k) (keys + 32 * ix[i].acc[k])
    char a1[48], a2[48], amount[40];
    if (prog == P_SYSTEM && dn == 12 && d[0] == 2 && !d[1] && !d[2] && !d[3] && ix[i].nacc >= 2) {
      wl_format_u64(le64(d + 4), 9, amount, sizeof(amount));
      wl_sol_address(ACC(1), a1, sizeof(a1));
      pg = wl_review_page(rv, "Send SOL", WL_TONE_NORMAL);
      wl_page_line(pg, "%s SOL", amount);
      wl_page_line(pg, "to");
      wl_page_wrap(pg, a1, WL_ADDR_CHUNK);
      if (memcmp(ACC(0), me, 32)) wl_page_line(pg, "(from another account!)");
      wl_review_add_recipient(rv, a1);
    } else if ((prog == P_TOKEN || prog == P_TOKEN22) && dn == 10 && d[0] == 12 && ix[i].nacc >= 4) {
      wl_format_u64(le64(d + 1), d[9], amount, sizeof(amount));
      wl_sol_address(ACC(1), a1, sizeof(a1));
      wl_sol_address(ACC(2), a2, sizeof(a2));
      pg = wl_review_page(rv, "Send token", WL_TONE_NORMAL);
      wl_page_line(pg, "%s tokens", amount);
      wl_page_line(pg, "mint");
      wl_page_wrap(pg, a1, WL_ADDR_CHUNK);
      wl_page_line(pg, "to token account");
      wl_page_wrap(pg, a2, WL_ADDR_CHUNK);
    } else if (prog == P_ATA && (dn == 0 || (dn == 1 && d[0] <= 1)) && ix[i].nacc >= 4) {
      wl_sol_address(ACC(2), a1, sizeof(a1));
      wl_sol_address(ACC(3), a2, sizeof(a2));
      pg = wl_review_page(rv, "Create account", WL_TONE_NORMAL);
      wl_page_line(pg, "Token account owned by");
      wl_page_wrap(pg, a1, WL_ADDR_CHUNK);
      wl_page_line(pg, "for mint");
      wl_page_wrap(pg, a2, WL_ADDR_CHUNK);
      wl_page_line(pg, "Costs ~0.002 SOL rent");
      wl_review_add_recipient(rv, a1);
    } else if (prog == P_COMPUTE && dn >= 1 && (d[0] == 2 || d[0] == 3)) {
      if (d[0] == 3 && dn == 9) {
        pg = wl_review_page(rv, "Priority fee", WL_TONE_NORMAL);
        wl_page_line(pg, "%llu micro-lamports", (unsigned long long)le64(d + 1));
        wl_page_line(pg, "per compute unit");
      }
    } else if (!blind) {
      char prog_s[48] = "(lookup table)";
      if (ix[i].prog < nkeys) wl_sol_address(keys + 32 * ix[i].prog, prog_s, sizeof(prog_s));
      FAIL(WL_VERDICT_UNSUPPORTED, "Cannot decode program %.8s... Blind signing is disabled on the device.", prog_s);
    } else {
      wl_sol_address(keys + 32 * ix[i].prog, a1, sizeof(a1));
      pg = wl_review_page(rv, "BLIND SIGNING", WL_TONE_DANGER);
      wl_page_line(pg, "Unknown program");
      wl_page_wrap(pg, a1, WL_ADDR_CHUNK);
      wl_page_line(pg, "Reject unless certain.");
    }
#undef ACC
  }
  if (rv->overflow) FAIL(WL_VERDICT_UNSUPPORTED, "too much to review on the device");
  return WL_VERDICT_OK;
}

void wl_sol_sign(const uint8_t sk[32], const uint8_t *msg, size_t len, uint8_t sig[64]) { ed25519_sign(msg, len, sk, sig); }
