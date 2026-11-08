/*
 * Protocol v1 request handling. The phone is untrusted: every field is
 * validated, every key is re-derived on the device, and everything that
 * reveals a receive address, moves funds or changes state waits for the
 * user on the device screen (ui_prompt).
 */
#include "app/proto.h"

#include <stdio.h>
#include <string.h>

#include "app/firmware.h"
#include "app/hal.h"
#include "app/noise.h"
#include "app/ui.h"
#include "app/vault.h"
#include "rand.h"
#include "sha2.h"
#include "wallet/address.h"
#include "wallet/btc.h"
#include "wallet/cbor.h"
#include "wallet/evm.h"
#include "wallet/keys.h"
#include "wallet/secure.h"
#include "wallet/sol.h"

#define MAX_FRAME 512
#define OUT_MAX (56u * 1024u)
#define MAX_ACCOUNTS 20
#define MAX_PLAN_STEPS 8

/* ---------------------------------------------------------------- state */
static struct {
  uint8_t buf[PROTO_MSG_MAX + NOISE_TAG];
  size_t len, filled;
  uint8_t seq;
  bool active;
} RX;

typedef enum { REQ_NONE, REQ_PAIR_THEN_DISPATCH } stage_t;

static struct {
  bool active;
  uint64_t id;
  uint8_t msg[PROTO_MSG_MAX];
  size_t len;
  wl_cbor_t root;
  char type[24];
  bool sweep_wallet;
  int scheme;
  uint32_t account;
  wl_review_t rv;
  wl_btc_tx_t btc;
  char address[WL_ADDRESS_MAX_LEN];
  bool pairing; /* the prompt on screen is the pairing prompt */
} P;

static struct {
  bool approved;
  uint8_t nsteps;
  char to[MAX_PLAN_STEPS][WL_ADDRESS_MAX_LEN];
} PLAN;

static bool paired, connected;
static uint8_t out[OUT_MAX + NOISE_TAG]; /* room to seal in place */

/* ---------------------------------------------------------------- secure channel (protocol section 3)
   WL_PROTO_NOISE (device builds): every message after the XX handshake is encrypted end to end
   with the app instance's own static key; the device pins that key when the user accepts the
   pairing code (derived from the handshake hash, so a man in the middle shows a different
   code). Without it (emulator): plaintext, development only. */
#ifdef WL_PROTO_NOISE
enum { NZ_WAIT1, NZ_WAIT3, NZ_OPEN };
static struct {
  int state;
  noise_hs_t hs;
  noise_cs_t tx, rx;
  uint8_t hash[32];
  uint8_t peer[32];   /* this session's app static key */
  bool peer_paired;   /* ... and it is the paired one */
  bool pair_asked;    /* the pairing prompt was shown for this connection (no repeats) */
} NZ;
static uint8_t dev_static[32]; /* device identity (demo: RAM, new at every start) */
static uint8_t paired_key[32]; /* the one paired app */

static void noise_begin(void) {
  uint8_t e[32];
  random_buffer(e, sizeof(e));
  wl_wipe(&NZ, sizeof(NZ));
  noise_xx_init(&NZ.hs, false, dev_static, e);
  wl_wipe(e, sizeof(e));
  NZ.state = NZ_WAIT1;
}
static bool session_paired(void) { return NZ.state == NZ_OPEN && NZ.peer_paired; }
static bool was_unlocked;
#else
static bool session_paired(void) { return paired; }
#endif

bool proto_paired(void) { return paired; }
bool proto_waiting_user(void) { return P.active; }

/* ---------------------------------------------------------------- sending */
static void frame_out(const uint8_t *m, size_t n) {
  size_t fs = hal_link_frame_size();
  if (fs < 20 || fs > MAX_FRAME || !connected) return;
  uint8_t f[MAX_FRAME];
  size_t first = n < fs - 5 ? n : fs - 5;
  f[0] = (uint8_t)(0x80 | (first == n ? 0x40 : 0));
  f[1] = (uint8_t)(n >> 24), f[2] = (uint8_t)(n >> 16), f[3] = (uint8_t)(n >> 8), f[4] = (uint8_t)n;
  memcpy(f + 5, m, first);
  hal_link_send(f, 5 + first);
  size_t off = first;
  uint8_t seq = 1;
  while (off < n) {
    size_t k = n - off < fs - 2 ? n - off : fs - 2;
    f[0] = off + k == n ? 0x40 : 0;
    f[1] = seq;
    memcpy(f + 2, m + off, k);
    hal_link_send(f, 2 + k);
    off += k;
    seq = seq == 255 ? 1 : (uint8_t)(seq + 1);
  }
}

/* m must have room for NOISE_TAG more bytes (it is sealed in place). */
static void send_message(uint8_t *m, size_t n) {
#ifdef WL_PROTO_NOISE
  if (NZ.state != NZ_OPEN) return; /* nothing leaves the device before the secure channel exists */
  noise_seal(&NZ.tx, m, n);
  n += NOISE_TAG;
#endif
  frame_out(m, n);
}

static void begin_ok(wl_cbor_w_t *w, uint64_t id, size_t result_pairs) {
  wl_cbor_w_init(w, out, OUT_MAX);
  wl_cbor_w_map(w, 3);
  wl_cbor_w_kv_uint(w, "id", id);
  wl_cbor_w_kv_bool(w, "ok", true);
  wl_cbor_w_text(w, "result");
  wl_cbor_w_map(w, result_pairs);
}

static void finish(wl_cbor_w_t *w) {
  if (w->overflow) return;
  send_message(w->buf, w->len);
}

static void send_error(uint64_t id, const char *code, const char *message) {
  wl_cbor_w_t w;
  wl_cbor_w_init(&w, out, OUT_MAX);
  wl_cbor_w_map(&w, 3);
  wl_cbor_w_kv_uint(&w, "id", id);
  wl_cbor_w_kv_bool(&w, "ok", false);
  wl_cbor_w_text(&w, "error");
  wl_cbor_w_map(&w, 2);
  wl_cbor_w_kv_text(&w, "code", code);
  wl_cbor_w_kv_text(&w, "message", message);
  hal_log("-> #%llu %s: %s", (unsigned long long)id, code, message);
  finish(&w);
}

static void send_event_detail(const char *event, bool with_id, uint64_t id, const char *detail) {
  wl_cbor_w_t w;
  wl_cbor_w_init(&w, out, OUT_MAX);
  wl_cbor_w_map(&w, 2 + (with_id ? 1 : 0) + (detail ? 1 : 0));
  wl_cbor_w_kv_text(&w, "type", "event");
  wl_cbor_w_kv_text(&w, "event", event);
  if (with_id) wl_cbor_w_kv_uint(&w, "id", id);
  if (detail) wl_cbor_w_kv_text(&w, "detail", detail);
  finish(&w);
}

static void send_event(const char *event, bool with_id, uint64_t id) { send_event_detail(event, with_id, id, NULL); }

static void end_request(void) {
  wl_wipe(&P, sizeof(P));
  ui_home();
}

/* ---------------------------------------------------------------- field helpers */
static bool get_uint(const char *k, uint64_t max, uint64_t *v) {
  wl_cbor_t x;
  return wl_cbor_map_get(P.root, k, &x) && wl_cbor_uint(x, v) && *v <= max;
}

static bool get_wallet(void) {
  wl_cbor_t v;
  if (!wl_cbor_map_get(P.root, "wallet", &v)) return false;
  if (wl_cbor_text_eq(v, "active")) P.sweep_wallet = false;
  else if (wl_cbor_text_eq(v, "sweep-source")) P.sweep_wallet = true;
  else return false;
  return true;
}

static bool get_net(wl_net_t *net) {
  wl_cbor_t v;
  if (!wl_cbor_map_get(P.root, "net", &v)) return false;
  if (wl_cbor_text_eq(v, "main")) *net = WL_NET_MAIN;
  else if (wl_cbor_text_eq(v, "test")) *net = WL_NET_TEST;
  else return false;
  return true;
}

static bool get_scheme(void) {
  char name[24];
  if (!wl_cbor_get_str(P.root, "scheme", name, sizeof(name))) return false;
  P.scheme = wl_scheme_by_name(name);
  return P.scheme >= 0;
}

static const uint8_t *wallet_seed(void) { return P.sweep_wallet ? vault_sweep_seed() : vault_seed(); }

static void path_string(wl_scheme_t s, wl_net_t net, uint32_t account, size_t depth_limit, char *o, size_t n) {
  uint32_t path[WL_MAX_PATH_DEPTH];
  size_t depth = 0;
  if (s == WL_SCHEME_SOL_CLI_SEED) {
    snprintf(o, n, "seed[0:32]");
    return;
  }
  wl_scheme_path(s, net, account, 0, 0, path, &depth);
  if (depth_limit && depth > depth_limit) depth = depth_limit;
  wl_path_to_string(path, depth, o, n);
}

static void to_hex(const uint8_t *b, size_t n, char *o) {
  static const char HEX[] = "0123456789abcdef";
  *o++ = '0', *o++ = 'x';
  for (size_t i = 0; i < n; i++) *o++ = HEX[b[i] >> 4], *o++ = HEX[b[i] & 15];
  *o = 0;
}

/* Sweep: the signed transaction must pay the destination of the approved step. */
static bool plan_step_ok(void) {
  if (!P.sweep_wallet) return true;
  uint64_t step;
  if (!PLAN.approved || !get_uint("planStep", MAX_PLAN_STEPS - 1, &step) || step >= PLAN.nsteps) return false;
  for (uint8_t i = 0; i < P.rv.nrecipients; i++) {
    const char *r = P.rv.recipients[i], *t = PLAN.to[step];
    if (wl_evm_same_address(r, t) || strcmp(r, t) == 0) return true;
  }
  return false;
}

/* ---------------------------------------------------------------- approval results */
static void approved_show_address(bool ok) {
  if (!ok) {
    send_error(P.id, "USER_REJECTED", "Rejected on device");
  } else {
    wl_cbor_w_t w;
    begin_ok(&w, P.id, 2);
    wl_cbor_w_kv_text(&w, "address", P.address);
    wl_cbor_w_kv_bool(&w, "confirmed", true);
    finish(&w);
  }
  end_request();
}

static void approved_btc(bool ok) {
  if (!ok) {
    send_error(P.id, "USER_REJECTED", "Rejected on device");
    end_request();
    return;
  }
  const uint8_t *seed = wallet_seed();
  wl_cbor_t v;
  const uint8_t *psbt;
  size_t plen, slen;
  static uint8_t signed_psbt[PROTO_MSG_MAX + 4096];
  if (!seed || !wl_cbor_map_get(P.root, "psbt", &v) || !wl_cbor_bytes(v, &psbt, &plen) ||
      wl_btc_sign(seed, &P.btc, psbt, plen, signed_psbt, sizeof(signed_psbt), &slen) != WL_OK) {
    send_error(P.id, "INTERNAL", "signing failed");
  } else {
    wl_cbor_w_t w;
    begin_ok(&w, P.id, 1);
    wl_cbor_w_kv_bytes(&w, "psbt", signed_psbt, slen);
    finish(&w);
    hal_log("-> #%llu signed PSBT (%u inputs)", (unsigned long long)P.id, (unsigned)P.btc.nin);
  }
  end_request();
}

static void approved_evm(bool ok) {
  if (!ok) {
    send_error(P.id, "USER_REJECTED", "Rejected on device");
    end_request();
    return;
  }
  uint8_t priv[32], r[32], s[32], y = 0;
  wl_cbor_t v;
  const uint8_t *tx;
  size_t tlen;
  const uint8_t *seed = wallet_seed();
  if (!seed || !wl_cbor_map_get(P.root, "tx", &v) || !wl_cbor_bytes(v, &tx, &tlen) ||
      wl_scheme_privkey(seed, (wl_scheme_t)P.scheme, WL_NET_MAIN, P.account, 0, 0, priv) != WL_OK ||
      wl_evm_sign(priv, tx, tlen, r, s, &y) != WL_OK) {
    send_error(P.id, "INTERNAL", "signing failed");
  } else {
    char rh[67], sh[67];
    to_hex(r, 32, rh);
    to_hex(s, 32, sh);
    wl_cbor_w_t w;
    begin_ok(&w, P.id, 3);
    wl_cbor_w_kv_text(&w, "r", rh);
    wl_cbor_w_kv_text(&w, "s", sh);
    wl_cbor_w_kv_uint(&w, "yParity", y);
    finish(&w);
    hal_log("-> #%llu signed EVM tx", (unsigned long long)P.id);
  }
  wl_wipe(priv, sizeof(priv));
  end_request();
}

static void approved_sol(bool ok) {
  if (!ok) {
    send_error(P.id, "USER_REJECTED", "Rejected on device");
    end_request();
    return;
  }
  uint8_t sk[32], pub[32], sig[64];
  wl_cbor_t v;
  const uint8_t *m;
  size_t mlen;
  const uint8_t *seed = wallet_seed();
  if (!seed || !wl_cbor_map_get(P.root, "message", &v) || !wl_cbor_bytes(v, &m, &mlen) ||
      wl_scheme_privkey(seed, (wl_scheme_t)P.scheme, WL_NET_MAIN, P.account, 0, 0, sk) != WL_OK ||
      wl_sol_pubkey(seed, (wl_scheme_t)P.scheme, P.account, pub) != WL_OK) {
    send_error(P.id, "INTERNAL", "signing failed");
  } else {
    wl_sol_sign(sk, m, mlen, sig);
    char pk[48];
    wl_sol_address(pub, pk, sizeof(pk));
    wl_cbor_w_t w;
    begin_ok(&w, P.id, 2);
    wl_cbor_w_kv_bytes(&w, "signature", sig, 64);
    wl_cbor_w_kv_text(&w, "publicKey", pk);
    finish(&w);
    hal_log("-> #%llu signed Solana message", (unsigned long long)P.id);
  }
  wl_wipe(sk, sizeof(sk));
  end_request();
}

static void approved_plan(bool ok) {
  if (!ok) {
    send_error(P.id, "USER_REJECTED", "Rejected on device");
    end_request();
    return;
  }
  wl_cbor_t plan, steps, st, to;
  wl_cbor_map_get(P.root, "plan", &plan);
  wl_cbor_map_get(plan, "steps", &steps);
  size_t n = 0;
  wl_cbor_array(steps, &n);
  memset(&PLAN, 0, sizeof(PLAN));
  for (size_t i = 0; i < n; i++) {
    wl_cbor_array_at(steps, i, &st);
    const char *s;
    size_t sl;
    if (wl_cbor_map_get(st, "to", &to) && wl_cbor_text(to, &s, &sl) && sl < WL_ADDRESS_MAX_LEN) memcpy(PLAN.to[i], s, sl);
  }
  PLAN.nsteps = (uint8_t)n;
  PLAN.approved = true;
  /* Plan hash: SHA-256 of the plan exactly as received (CBOR bytes). */
  uint8_t h[32];
  char hex[67];
  sha256_Raw(plan.p, plan.n, h);
  to_hex(h, 32, hex);
  wl_cbor_w_t w;
  begin_ok(&w, P.id, 2);
  wl_cbor_w_kv_bool(&w, "approved", true);
  wl_cbor_w_kv_text(&w, "planHash", hex + 2);
  finish(&w);
  hal_log("sweep plan approved: %u step(s)", (unsigned)n);
  end_request();
}

static void approved_forget(bool ok) {
  if (!ok) {
    send_error(P.id, "USER_REJECTED", "Rejected on device");
  } else if (vault_set_sweep(NULL, 0) != VAULT_OK) {
    send_error(P.id, "INTERNAL", "could not erase");
  } else {
    memset(&PLAN, 0, sizeof(PLAN));
    wl_cbor_w_t w;
    begin_ok(&w, P.id, 1);
    wl_cbor_w_kv_bool(&w, "erased", true);
    finish(&w);
    hal_log("imported (sweep source) seed erased");
  }
  end_request();
}

static void ask(bool need_pin, ui_done_fn done) {
  /* detail "pair": the prompt is the pairing code, not the request itself (the app keeps
     its full-screen overlay for pairing, but shows the address next to the device for
     show-address so the user can compare the two) */
  send_event_detail("awaiting-user", true, P.id, P.pairing ? "pair" : NULL);
  ui_prompt(&P.rv, need_pin, done);
}

/* ---------------------------------------------------------------- sweep destination check */
/* Is addr one of the ACTIVE wallet's own receive addresses? */
static bool owns_active(const char *addr) {
  const uint8_t *seed = vault_seed();
  char a[WL_ADDRESS_MAX_LEN];
  if (!seed) return false;
  bool evm = addr[0] == '0' && addr[1] == 'x';
  bool btc = !evm && (!strncmp(addr, "bc1", 3) || !strncmp(addr, "tb1", 3) || strlen(addr) <= 35);
  for (int s = 0; s < WL_SCHEME_COUNT; s++) {
    const wl_scheme_info_t *info = wl_scheme_info((wl_scheme_t)s);
    if (info->chain == WL_CHAIN_BTC && btc) {
      for (int net = 0; net < 2; net++)
        for (uint32_t i = 0; i < 20; i++)
          if (wl_address(seed, (wl_scheme_t)s, (wl_net_t)net, 0, 0, i, a, sizeof(a)) == WL_OK && !strcmp(a, addr)) return true;
    } else if (info->chain == WL_CHAIN_EVM && evm) {
      for (uint32_t acct = 0; acct < 5; acct++)
        if (wl_address(seed, (wl_scheme_t)s, WL_NET_MAIN, acct, 0, 0, a, sizeof(a)) == WL_OK && wl_evm_same_address(a, addr))
          return true;
    } else if (info->chain == WL_CHAIN_SOL && !evm) {
      for (uint32_t acct = 0; acct <= info->max_account && acct < 5; acct++)
        if (wl_address(seed, (wl_scheme_t)s, WL_NET_MAIN, acct, 0, 0, a, sizeof(a)) == WL_OK && !strcmp(a, addr)) return true;
    }
  }
  return false;
}

/* ---------------------------------------------------------------- dispatch (after pairing) */
#define BAD(msg)                                    \
  do {                                              \
    send_error(P.id, "INVALID_REQUEST", msg);       \
    end_request();                                  \
    return;                                         \
  } while (0)

static void refuse(wl_verdict_t v, const char *why) {
  send_error(P.id, v == WL_VERDICT_UNSUPPORTED ? "UNSUPPORTED" : "INVALID_REQUEST", why);
  end_request();
}

static void dispatch(void) {
  const char *t = P.type;
  char err[128];
  uint64_t u;
  wl_net_t net = WL_NET_MAIN;

  if (!strcmp(t, "get-accounts")) {
    wl_cbor_t arr, it;
    size_t n;
    if (!get_wallet() || !get_net(&net) || !get_scheme()) BAD("bad parameters");
    if (!wallet_seed()) BAD(P.sweep_wallet ? "no sweep-source wallet on device" : "no wallet");
    if (!wl_cbor_map_get(P.root, "accounts", &arr) || !wl_cbor_array(arr, &n) || n == 0 || n > MAX_ACCOUNTS) BAD("bad accounts");
    const wl_scheme_info_t *info = wl_scheme_info((wl_scheme_t)P.scheme);
    wl_cbor_w_t w;
    begin_ok(&w, P.id, 1);
    wl_cbor_w_text(&w, "accounts");
    wl_cbor_w_array(&w, n);
    for (size_t i = 0; i < n; i++) {
      if (!wl_cbor_array_at(arr, i, &it) || !wl_cbor_uint(it, &u) || u > info->max_account) BAD("bad account number");
      uint32_t acct = (uint32_t)u;
      char path[64];
      if (info->chain == WL_CHAIN_BTC) {
        wl_account_key_t k;
        if (wl_account_key(wallet_seed(), (wl_scheme_t)P.scheme, net, acct, &k) != WL_OK) BAD("derivation failed");
        path_string((wl_scheme_t)P.scheme, net, acct, 3, path, sizeof(path));
        wl_cbor_w_map(&w, 6);
        wl_cbor_w_kv_uint(&w, "account", acct);
        wl_cbor_w_kv_text(&w, "path", path);
        wl_cbor_w_kv_text(&w, "kind", "xpub");
        wl_cbor_w_kv_bytes(&w, "publicKey", k.public_key, 33);
        wl_cbor_w_kv_bytes(&w, "chainCode", k.chain_code, 32);
        wl_cbor_w_kv_uint(&w, "masterFingerprint", k.master_fingerprint);
      } else {
        char a[WL_ADDRESS_MAX_LEN];
        if (wl_address(wallet_seed(), (wl_scheme_t)P.scheme, WL_NET_MAIN, acct, 0, 0, a, sizeof(a)) != WL_OK)
          BAD("derivation failed");
        path_string((wl_scheme_t)P.scheme, WL_NET_MAIN, acct, 0, path, sizeof(path));
        wl_cbor_w_map(&w, 4);
        wl_cbor_w_kv_uint(&w, "account", acct);
        wl_cbor_w_kv_text(&w, "path", path);
        wl_cbor_w_kv_text(&w, "kind", "address");
        wl_cbor_w_kv_text(&w, "address", a);
      }
    }
    finish(&w);
    end_request();
    return;
  }

  if (!strcmp(t, "show-address")) {
    uint64_t acct, change, index;
    if (!get_wallet() || !get_net(&net) || !get_scheme() || !get_uint("account", 255, &acct) ||
        !get_uint("change", 1, &change) || !get_uint("index", 0x7FFFFFFF, &index))
      BAD("bad parameters");
    if (!wallet_seed()) BAD("no such wallet on device");
    const wl_scheme_info_t *info = wl_scheme_info((wl_scheme_t)P.scheme);
    wl_net_t anet = info->chain == WL_CHAIN_BTC ? net : WL_NET_MAIN;
    if (wl_address(wallet_seed(), (wl_scheme_t)P.scheme, anet, (uint32_t)acct, (uint32_t)change, (uint32_t)index,
                   P.address, sizeof(P.address)) != WL_OK)
      BAD("bad account/change/index for this scheme");
    wl_review_init(&P.rv);
    wl_page_t *p = wl_review_page(&P.rv, "Receive", WL_TONE_NORMAL);
    wl_page_wrap(p, P.address, WL_ADDR_CHUNK);
    wl_page_line(p, " ");
    wl_page_line(p, "%s", info->label);
    if (info->chain == WL_CHAIN_BTC)
      wl_page_line(p, "account %u  /%u/%u", (unsigned)acct, (unsigned)change, (unsigned)index);
    else
      wl_page_line(p, "account %u", (unsigned)acct);
    if (net == WL_NET_TEST) wl_page_line(p, "TESTNET");
    if (P.sweep_wallet) wl_page_line(p, "(imported seed)");
    p = wl_review_page(&P.rv, "Matches the app?", WL_TONE_NORMAL);
    wl_page_line(p, "Confirm only if the");
    wl_page_line(p, "address on your phone");
    wl_page_line(p, "is exactly the same.");
    ask(false, approved_show_address);
    return;
  }

  if (!strcmp(t, "sign-btc")) {
    wl_cbor_t v;
    const uint8_t *psbt;
    size_t plen;
    if (!get_wallet() || !get_net(&net) || !wl_cbor_map_get(P.root, "psbt", &v) || !wl_cbor_bytes(v, &psbt, &plen))
      BAD("bad parameters");
    if (!wallet_seed()) BAD("no such wallet on device");
    wl_verdict_t vd = wl_btc_analyze(wallet_seed(), net, psbt, plen, &P.btc, &P.rv, err, sizeof(err));
    if (vd != WL_VERDICT_OK) {
      refuse(vd, err);
      return;
    }
    if (!plan_step_ok()) BAD(P.sweep_wallet ? "transaction does not match the approved sweep plan" : "bad plan step");
    ask(false, approved_btc);
    return;
  }

  if (!strcmp(t, "sign-evm")) {
    wl_cbor_t v, hv;
    const uint8_t *tx;
    size_t tlen;
    uint64_t chain;
    if (!get_wallet() || !get_scheme() || wl_scheme_info((wl_scheme_t)P.scheme)->chain != WL_CHAIN_EVM ||
        !get_uint("account", 255, &u) || !get_uint("chainId", 0xFFFFFFFFFFFFull, &chain) ||
        !wl_cbor_map_get(P.root, "tx", &v) || !wl_cbor_bytes(v, &tx, &tlen))
      BAD("bad parameters");
    P.account = (uint32_t)u;
    if (!wallet_seed()) BAD("no such wallet on device");
    wl_evm_hints_t hints = {{0}, -1};
    if (wl_cbor_map_get(P.root, "hints", &hv) && wl_cbor_is_map(hv)) {
      char sym[16];
      uint64_t dec;
      wl_cbor_t dv;
      if (wl_cbor_get_str(hv, "tokenSymbol", sym, sizeof(sym))) {
        size_t k = 0;
        for (size_t i = 0; sym[i] && k < sizeof(hints.symbol) - 1; i++)
          if (sym[i] > 0x20 && sym[i] < 0x7f) hints.symbol[k++] = sym[i]; /* printable only */
        hints.symbol[k] = 0;
      }
      if (wl_cbor_map_get(hv, "tokenDecimals", &dv) && wl_cbor_uint(dv, &dec) && dec <= 36) hints.decimals = (int)dec;
    }
    char from[WL_ADDRESS_MAX_LEN];
    if (wl_address(wallet_seed(), (wl_scheme_t)P.scheme, WL_NET_MAIN, P.account, 0, 0, from, sizeof(from)) != WL_OK)
      BAD("bad account");
    wl_verdict_t vd = wl_evm_analyze(tx, tlen, chain, from, P.account, vault_blind_signing(), &hints, &P.rv, err, sizeof(err));
    if (vd != WL_VERDICT_OK) {
      refuse(vd, err);
      return;
    }
    if (!plan_step_ok()) BAD(P.sweep_wallet ? "transaction does not match the approved sweep plan" : "bad plan step");
    ask(false, approved_evm);
    return;
  }

  if (!strcmp(t, "sign-sol")) {
    wl_cbor_t v;
    const uint8_t *m;
    size_t mlen;
    if (!get_wallet() || !get_scheme() || wl_scheme_info((wl_scheme_t)P.scheme)->chain != WL_CHAIN_SOL ||
        !get_uint("account", 255, &u) || !get_net(&net) || !wl_cbor_map_get(P.root, "message", &v) ||
        !wl_cbor_bytes(v, &m, &mlen))
      BAD("bad parameters");
    P.account = (uint32_t)u;
    if (!wallet_seed()) BAD("no such wallet on device");
    uint8_t me[32];
    if (wl_sol_pubkey(wallet_seed(), (wl_scheme_t)P.scheme, P.account, me) != WL_OK) BAD("bad account");
    wl_verdict_t vd = wl_sol_analyze(m, mlen, me, net == WL_NET_TEST, P.account, vault_blind_signing(), &P.rv, err, sizeof(err));
    if (vd != WL_VERDICT_OK) {
      refuse(vd, err);
      return;
    }
    if (!plan_step_ok()) BAD(P.sweep_wallet ? "transaction does not match the approved sweep plan" : "bad plan step");
    ask(false, approved_sol);
    return;
  }

  if (!strcmp(t, "sweep-plan")) {
    wl_cbor_t plan, steps, st;
    size_t n;
    if (!vault_sweep_seed()) BAD("no imported seed to sweep");
    if (!wl_cbor_map_get(P.root, "plan", &plan) || !wl_cbor_map_get(plan, "steps", &steps) || !wl_cbor_array(steps, &n) ||
        n == 0 || n > MAX_PLAN_STEPS)
      BAD("bad plan");
    wl_review_init(&P.rv);
    for (size_t i = 0; i < n; i++) {
      char chain[24], asset[16], amount[32], to[WL_ADDRESS_MAX_LEN], fee[32], title[28], m[96];
      if (!wl_cbor_array_at(steps, i, &st) || !wl_cbor_get_str(st, "chain", chain, sizeof(chain)) ||
          !wl_cbor_get_str(st, "asset", asset, sizeof(asset)) || !wl_cbor_get_str(st, "amount", amount, sizeof(amount)) ||
          !wl_cbor_get_str(st, "to", to, sizeof(to)) || !wl_cbor_get_str(st, "feeEstimate", fee, sizeof(fee)))
        BAD("bad plan step");
      if (!owns_active(to)) {
        snprintf(m, sizeof(m), "destination %.12s... is not your new wallet", to);
        BAD(m);
      }
      snprintf(title, sizeof(title), "Sweep %u of %u", (unsigned)(i + 1), (unsigned)n);
      wl_page_t *p = wl_review_page(&P.rv, title, WL_TONE_NORMAL);
      wl_page_line(p, "%s %s", amount, asset);
      wl_page_line(p, "%s", chain);
      wl_page_line(p, "to YOUR new wallet:");
      wl_page_wrap(p, to, WL_ADDR_CHUNK);
      wl_page_line(p, "fee ~ %s", fee);
    }
    ask(true, approved_plan);
    return;
  }

  if (!strcmp(t, "forget-sweep-source")) {
    if (!vault_has_sweep()) {
      wl_cbor_w_t w;
      begin_ok(&w, P.id, 1);
      wl_cbor_w_kv_bool(&w, "erased", true);
      finish(&w);
      end_request();
      return;
    }
    wl_review_init(&P.rv);
    wl_page_t *p = wl_review_page(&P.rv, "Erase old seed?", WL_TONE_WARN);
    wl_page_line(p, "The imported seed will");
    wl_page_line(p, "be deleted from this");
    wl_page_line(p, "device forever.");
    ask(true, approved_forget);
    return;
  }

  BAD("unknown request");
}

static void pairing_done(bool ok) {
  P.pairing = false;
  bool standalone = P.type[0] == 0; /* asked on connect, no request waiting behind it */
  if (!ok) {
    if (standalone) send_event("pair-rejected", false, 0);
    else send_error(P.id, "USER_REJECTED", "pairing rejected on device");
    end_request();
    return;
  }
  paired = true;
#ifdef WL_PROTO_NOISE
  memcpy(paired_key, NZ.peer, 32); /* replaces any previously paired app */
  NZ.peer_paired = true;
#endif
  ui_set_status(connected, true);
  hal_log("phone paired");
  if (standalone) {
    send_event("paired", false, 0);
    end_request();
    return;
  }
  dispatch();
}

/* The pairing question with the 6-digit code from the handshake (the app shows the same code). */
static void ask_pairing(void) {
#ifdef WL_PROTO_NOISE
  uint32_t code = noise_pair_code(NZ.hash);
  NZ.pair_asked = true;
#else
  uint32_t code = 100000 + random32() % 900000;
#endif
  wl_review_init(&P.rv);
  wl_page_t *p = wl_review_page(&P.rv, "Allow pairing?", WL_TONE_NORMAL);
  wl_page_line(p, "Pair this phone with the");
  wl_page_line(p, "device? It must show:");
  wl_page_line(p, "\x01%06lu", (unsigned long)code);
  wl_page_line(p, "Not the same? Reject.");
  P.pairing = true;
  hal_log("pairing request, code %06lu", (unsigned long)code);
  ask(true, pairing_done);
}

/* ---------------------------------------------------------------- request entry */
static void handle(const uint8_t *msg, size_t len) {
  wl_cbor_t root, v;
  uint64_t id;
  char type[24];
  if (!wl_cbor_parse(msg, len, &root) || !wl_cbor_is_map(root) || !wl_cbor_map_get(root, "id", &v) ||
      !wl_cbor_uint(v, &id) || !wl_cbor_get_str(root, "type", type, sizeof(type))) {
    hal_log("malformed message dropped");
    return;
  }
  hal_log("<- #%llu %s", (unsigned long long)id, type);
  fw_activity();

  if (!strcmp(type, "get-info")) {
    wl_cbor_w_t w;
    begin_ok(&w, id, 6);
    wl_cbor_w_kv_text(&w, "model", FW_MODEL);
    wl_cbor_w_kv_text(&w, "firmware", FW_VERSION);
    wl_cbor_w_kv_uint(&w, "protocol", 1);
    wl_cbor_w_kv_bool(&w, "initialized", vault_initialized());
    wl_cbor_w_kv_bool(&w, "locked", !vault_unlocked());
    wl_cbor_w_kv_bool(&w, "hasSweepSource", vault_has_sweep());
    finish(&w);
    return;
  }
  if (P.active) {
    send_error(id, "BUSY", "device is waiting for the user");
    return;
  }
  if (!strcmp(type, "lock")) {
    fw_lock("app request");
    wl_cbor_w_t w;
    begin_ok(&w, id, 0);
    finish(&w);
    return;
  }
  if (!strcmp(type, "set-balance")) { /* main-page total from the paired app: display only */
    char text[20];
    if (!session_paired() || !vault_unlocked()) {
      send_error(id, "LOCKED", "unlock and pair first");
      return;
    }
    bool ok = wl_cbor_get_str(root, "text", text, sizeof(text)) && text[0];
    for (const char *c = text; ok && *c; c++) ok = strchr("0123456789$.,~ kKmMbB-", *c) != NULL;
    if (!ok) {
      send_error(id, "BAD_REQUEST", "text: up to 19 of 0-9 $ . , ~ k M B -");
      return;
    }
    ui_set_balance(text);
    wl_cbor_w_t w;
    begin_ok(&w, id, 0);
    finish(&w);
    return;
  }
  if (!vault_initialized()) {
    send_error(id, "NOT_INITIALIZED", "set up the device first");
    return;
  }
  if (!vault_unlocked()) {
    send_error(id, "LOCKED", "unlock the device with your PIN");
    return;
  }
  if (!ui_idle()) {
    send_error(id, "BUSY", "finish what is open on the device first");
    return;
  }

  /* Keep our own copy: the request must not change while the user reviews it. */
  memset(&P, 0, sizeof(P));
  P.active = true;
  P.id = id;
  memcpy(P.msg, msg, len);
  P.len = len;
  wl_cbor_parse(P.msg, P.len, &P.root);
  snprintf(P.type, sizeof(P.type), "%s", type);

  if (!session_paired()) {
    ask_pairing();
    return;
  }
  dispatch();
}

/* ---------------------------------------------------------------- secure channel in */
static void deliver(uint8_t *msg, size_t len) {
#ifndef WL_PROTO_NOISE
  if (len <= PROTO_MSG_MAX) handle(msg, len);
#else
  if (NZ.state == NZ_WAIT1) {
    uint8_t m2[NOISE_MSG2];
    if (noise_xx_read(&NZ.hs, msg, len) && noise_xx_write(&NZ.hs, m2, sizeof(m2)) == NOISE_MSG2) {
      frame_out(m2, sizeof(m2));
      NZ.state = NZ_WAIT3;
    } else {
      hal_log("secure channel: bad handshake start, reset");
      noise_begin();
    }
  } else if (NZ.state == NZ_WAIT3) {
    if (noise_xx_read(&NZ.hs, msg, len) && noise_xx_split(&NZ.hs, &NZ.tx, &NZ.rx, NZ.hash, NZ.peer)) {
      NZ.state = NZ_OPEN;
      NZ.peer_paired = paired && wl_ct_equal(NZ.peer, paired_key, 32);
      hal_log("secure channel open (%s app)", NZ.peer_paired ? "paired" : "unpaired");
      ui_set_status(connected, NZ.peer_paired);
    } else {
      hal_log("secure channel: handshake failed, reset");
      noise_begin();
    }
  } else if (noise_open(&NZ.rx, msg, len)) {
    if (len - NOISE_TAG <= PROTO_MSG_MAX) handle(msg, len - NOISE_TAG);
  } else {
    /* forged, replayed or out of order: the session is dead, the app must reconnect */
    hal_log("secure channel: message failed authentication, session closed");
    if (P.active) {
      ui_cancel_prompt();
      wl_wipe(&P, sizeof(P));
    }
    noise_begin();
  }
#endif
}

/* ---------------------------------------------------------------- framing */
static void rx_reset(void) {
  RX.active = false;
  RX.len = RX.filled = 0;
  RX.seq = 0;
}

static void rx_frame(const uint8_t *f, size_t n) {
  if (n < 2) {
    rx_reset();
    return;
  }
  bool last = f[0] & 0x40;
  const uint8_t *payload;
  size_t pn;
  if (f[0] & 0x80) {
    if (n < 5) {
      rx_reset();
      return;
    }
    size_t len = (size_t)f[1] << 24 | (size_t)f[2] << 16 | (size_t)f[3] << 8 | f[4];
    if (len > PROTO_MSG_MAX + NOISE_TAG) {
      hal_log("message too large (%u bytes), dropped", (unsigned)len);
      rx_reset();
      return;
    }
    RX.active = true, RX.len = len, RX.filled = 0, RX.seq = 0;
    payload = f + 5, pn = n - 5;
  } else {
    uint8_t expect = RX.seq == 255 ? 1 : (uint8_t)(RX.seq + 1);
    if (!RX.active || f[1] != expect) {
      hal_log("framing error, dropped");
      rx_reset();
      return;
    }
    RX.seq = expect;
    payload = f + 2, pn = n - 2;
  }
  if (pn > RX.len - RX.filled) {
    rx_reset();
    return;
  }
  memcpy(RX.buf + RX.filled, payload, pn);
  RX.filled += pn;
  if (last) {
    bool complete = RX.filled == RX.len;
    size_t len = RX.len;
    rx_reset();
    if (complete) deliver(RX.buf, len);
  } else if (RX.filled == RX.len) {
    rx_reset();
  }
}

void proto_poll(void) {
  uint8_t f[MAX_FRAME];
  size_t n;
  while ((n = hal_link_recv(f, sizeof(f))) > 0) rx_frame(f, n);
#ifdef WL_PROTO_NOISE
  /* A new (unpaired) app on an open channel: ask on the device right away, without waiting for a
     request, once per connection. Locked: the lock screen says so, and the question comes as
     soon as the wallet is unlocked. Pairing still needs the PIN (ask(true, ...)). */
  if (NZ.state == NZ_OPEN && !NZ.peer_paired && !NZ.pair_asked && !P.active && vault_unlocked() && ui_idle()) {
    memset(&P, 0, sizeof(P));
    P.active = true;
    ask_pairing();
  }
  /* Tell the app when the wallet gets unlocked, so it can reload without a button press. */
  bool u = vault_unlocked();
  if (u && !was_unlocked && NZ.state == NZ_OPEN) send_event("unlocked", false, 0);
  was_unlocked = u;
#endif
}

void proto_link_changed(bool up) {
  connected = up;
  rx_reset();
#ifdef WL_PROTO_NOISE
  noise_begin(); /* every connection starts a fresh handshake */
#endif
  if (!up && P.active) {
    ui_cancel_prompt();
    wl_wipe(&P, sizeof(P));
  }
  ui_set_status(connected, session_paired()); /* this connection's app, not "some app" */
}

void proto_locked(void) {
  if (P.active) {
    send_error(P.id, "LOCKED", "device locked");
    ui_cancel_prompt();
    wl_wipe(&P, sizeof(P));
  }
  memset(&PLAN, 0, sizeof(PLAN));
  ui_set_balance(NULL); /* the next unlock shows it again once the app reports it */
  send_event("locked", false, 0);
}

void proto_forget_pairing(void) {
  paired = false;
#ifdef WL_PROTO_NOISE
  wl_wipe(paired_key, sizeof(paired_key));
  NZ.peer_paired = false;
#endif
  memset(&PLAN, 0, sizeof(PLAN));
  ui_set_status(connected, paired);
}

void proto_init(void) {
  rx_reset();
  memset(&P, 0, sizeof(P));
  memset(&PLAN, 0, sizeof(PLAN));
  paired = connected = false;
#ifdef WL_PROTO_NOISE
  random_buffer(dev_static, sizeof(dev_static));
  noise_begin();
#endif
}
