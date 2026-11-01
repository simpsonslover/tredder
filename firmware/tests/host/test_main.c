/*
 * Host unit tests for the wallet core.  Build + run: python firmware/tools/host_test.py
 */
#include <stdio.h>
#include <string.h>

#include "bip32.h"
#include "curves.h"
#include "wallet/address.h"
#include "wallet/entropy.h"
#include "wallet/mnemonic.h"
#include "wallet/schemes.h"
#include "wallet/secure.h"
#include "vectors.h"

static int g_checks, g_fail;
static const char *g_test;

#define CHECK(cond, ...)                                              \
  do {                                                                \
    g_checks++;                                                       \
    if (!(cond)) {                                                    \
      g_fail++;                                                       \
      printf("  FAIL [%s] %s:%d: ", g_test, __FILE__, __LINE__);      \
      printf(__VA_ARGS__);                                            \
      printf("\n");                                                   \
    }                                                                 \
  } while (0)

#define N(a) (sizeof(a) / sizeof((a)[0]))

static size_t unhex(const char *h, uint8_t *out, size_t max) {
  size_t n = strlen(h) / 2;
  if (n > max) return 0;
  for (size_t i = 0; i < n; i++) {
    unsigned v;
    sscanf(h + 2 * i, "%2x", &v);
    out[i] = (uint8_t)v;
  }
  return n;
}

static void tohex(const uint8_t *b, size_t n, char *out) {
  for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
}

static int all_zero(const void *p, size_t n) {
  const uint8_t *b = p;
  for (size_t i = 0; i < n; i++)
    if (b[i]) return 0;
  return 1;
}

/* ------------------------------------------------------------------ BIP39 */

static void test_bip39_official(void) {
  g_test = "bip39_official";
  for (size_t v = 0; v < N(BIP39_VECTORS); v++) {
    const bip39_vec_t *t = &BIP39_VECTORS[v];
    uint8_t ent[32], seed[64];
    uint16_t idx[24];
    size_t words = 0, elen = unhex(t->entropy_hex, ent, sizeof(ent));
    char str[WL_MNEMONIC_MAX_LEN], hex[129];

    CHECK(wl_mnemonic_from_entropy(ent, elen, idx, &words) == WL_OK, "vec %zu from_entropy", v);
    CHECK(wl_mnemonic_to_string(idx, words, str, sizeof(str)) == WL_OK && strcmp(str, t->mnemonic) == 0,
          "vec %zu mnemonic: got '%s'", v, str);
    CHECK(wl_mnemonic_check_indices(idx, words) == WL_OK, "vec %zu checksum", v);
    CHECK(wl_mnemonic_to_seed(idx, words, "TREZOR", seed) == WL_OK, "vec %zu to_seed", v);
    tohex(seed, 64, hex);
    CHECK(strcmp(hex, t->seed_hex) == 0, "vec %zu seed", v);

    uint16_t idx2[24];
    size_t w2 = 0, bad = 99;
    CHECK(wl_mnemonic_parse(t->mnemonic, idx2, &w2, &bad) == WL_OK && w2 == words &&
              memcmp(idx, idx2, words * 2) == 0 && bad == 0,
          "vec %zu parse", v);
  }
}

static void test_bip39_errors(void) {
  g_test = "bip39_errors";
  size_t words, bad;
  uint16_t idx[24];
  /* wrong word count */
  CHECK(wl_mnemonic_parse("abandon abandon abandon", idx, &words, &bad) == WL_ERR_WORD_COUNT, "3 words");
  CHECK(wl_mnemonic_parse("", idx, &words, &bad) == WL_ERR_WORD_COUNT, "empty");
  /* unknown word reported with its 1-based position */
  CHECK(wl_mnemonic_parse("abandon abandon abandon abandon abandon abandon abandon bitcoin abandon abandon abandon about",
                          idx, &words, &bad) == WL_ERR_UNKNOWN_WORD && bad == 8,
        "unknown word at 8, got %zu", bad);
  CHECK(wl_mnemonic_parse("abandonx abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about",
                          idx, &words, &bad) == WL_ERR_UNKNOWN_WORD && bad == 1, "over-long word");
  CHECK(wl_mnemonic_parse("Abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about",
                          idx, &words, &bad) == WL_ERR_UNKNOWN_WORD && bad == 1, "uppercase is not a word");
  /* valid words, bad checksum (last word changed) */
  CHECK(wl_mnemonic_parse("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon",
                          idx, &words, &bad) == WL_ERR_CHECKSUM, "checksum");
  /* Every length (12/15/18/21/24) validates, and single-word substitutions are
     caught at the rate the checksum allows: 1 - 2^-(words/3). */
  int lengths_seen = 0;
  for (size_t m = 0; m < N(DIFF_MNEMONICS); m++) {
    CHECK(wl_mnemonic_parse(DIFF_MNEMONICS[m], idx, &words, &bad) == WL_OK, "phrase %zu valid", m);
    lengths_seen |= 1 << (words / 3);
    int caught = 0, total = 0;
    for (size_t pos = 0; pos < words; pos++) {
      uint16_t save = idx[pos];
      for (uint16_t w = 0; w < 2048; w += 41) {
        if (w == save) continue;
        idx[pos] = w;
        total++;
        caught += wl_mnemonic_check_indices(idx, words) == WL_ERR_CHECKSUM;
      }
      idx[pos] = save;
    }
    double expect = 1.0 - 1.0 / (double)(1u << (words / 3));
    CHECK(caught > (int)(total * (expect - 0.04)), "%zu words: caught %d/%d (expect ~%.0f%%)", words, caught,
          total, expect * 100);
  }
  CHECK(lengths_seen == ((1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8)), "all five lengths tested");
  /* passphrase restrictions */
  uint8_t seed[64];
  CHECK(wl_mnemonic_parse(DIFF_MNEMONICS[0], idx, &words, &bad) == WL_OK, "reparse");
  CHECK(wl_mnemonic_to_seed(idx, words, "caf\xc3\xa9", seed) == WL_ERR_PASSPHRASE, "non-ASCII passphrase rejected");
  CHECK(wl_mnemonic_to_seed(idx, words, "tab\there", seed) == WL_ERR_PASSPHRASE, "control char rejected");
  char longp[WL_PASSPHRASE_MAX_LEN + 2];
  memset(longp, 'a', sizeof(longp) - 1);
  longp[sizeof(longp) - 1] = 0;
  CHECK(wl_mnemonic_to_seed(idx, words, longp, seed) == WL_ERR_PASSPHRASE, "too long rejected");
  CHECK(wl_mnemonic_from_entropy(seed, 17, idx, &words) == WL_ERR_ARG, "entropy 17 bytes rejected");
}

static void test_wordlist(void) {
  g_test = "wordlist";
  uint16_t out[8];
  /* Every word is reachable, round-trips, and is unique after 4 letters (BIP39 English property). */
  for (uint16_t i = 0; i < 2048; i++) {
    const char *w = wl_word(i);
    CHECK(wl_word_index(w) == i, "index of %s", w);
    char p[5] = {0};
    strncpy(p, w, 4);
    size_t n = wl_word_complete(p, out, N(out));
    if (strlen(w) >= 4) CHECK(n == 1 && out[0] == i, "'%s' unique after 4 letters (n=%zu)", w, n);
    else CHECK(n >= 1 && out[0] == i, "'%s' is first completion of itself", w);
  }
  CHECK(wl_word(2048) == NULL, "index 2048");
  CHECK(wl_word_index("zzz") == -1 && wl_word_index("") == -1 && wl_word_index("abandonabandon") == -1, "unknown");
  CHECK(wl_word_complete("", NULL, 0) == 2048, "empty prefix matches all");
  CHECK(wl_word_complete("q", NULL, 0) > 0 && wl_word_complete("qx", NULL, 0) == 0, "q / qx");
  uint32_t m = wl_word_next_letters("ab");
  CHECK(m == ((1u << ('a' - 'a')) | (1u << ('i' - 'a')) | (1u << ('l' - 'a')) | (1u << ('o' - 'a')) |
              (1u << ('s' - 'a')) | (1u << ('u' - 'a'))),
        "letters after 'ab' = abandon ability able about above absent absorb abstract absurd abuse: mask %08x", m);
  CHECK(wl_word_next_letters("zoo") == 0, "nothing after 'zoo'");
}

/* ------------------------------------------------------------------ SLIP-10 / BIP32 */

static void test_slip10_ed25519(void) {
  g_test = "slip10_ed25519";
  for (size_t v = 0; v < N(SLIP10_ED25519); v++) {
    const slip10_vec_t *t = &SLIP10_ED25519[v];
    uint8_t seed[64];
    size_t sl = unhex(t->seed_hex, seed, sizeof(seed));
    HDNode node;
    CHECK(hdnode_from_seed(seed, (int)sl, ED25519_NAME, &node) == 1, "from_seed");
    const char *p = t->path + 1;
    while (*p == '/') {
      unsigned long i = strtoul(p + 1, (char **)&p, 10);
      if (*p == '\'') p++;
      hdnode_private_ckd(&node, (uint32_t)i | WL_HARDENED);
    }
    hdnode_fill_public_key(&node);
    char hk[65], hp[67];
    tohex(node.private_key, 32, hk);
    tohex(node.public_key, 33, hp);
    CHECK(strcmp(hk, t->priv_hex) == 0, "%s priv", t->path);
    CHECK(strcmp(hp, t->pub_hex) == 0, "%s pub", t->path);
    wl_wipe(&node, sizeof(node));
  }
}

/* ------------------------------------------------------------------ addresses */

static void seed_for(int mi, int pi, uint8_t seed[64]) {
  uint16_t idx[24];
  size_t words, bad;
  CHECK(wl_mnemonic_parse(DIFF_MNEMONICS[mi], idx, &words, &bad) == WL_OK, "parse mnemonic %d", mi);
  CHECK(wl_mnemonic_to_seed(idx, words, DIFF_PASSPHRASES[pi], seed) == WL_OK, "seed %d/%d", mi, pi);
}

static void test_official_addresses(void) {
  g_test = "official_addresses";
  uint8_t seed[64];
  seed_for(0, 0, seed); /* abandon x11 about */
  for (size_t v = 0; v < N(OFFICIAL_ADDRESSES); v++) {
    const official_addr_t *t = &OFFICIAL_ADDRESSES[v];
    char a[WL_ADDRESS_MAX_LEN];
    wl_status_t st = wl_address(seed, t->scheme, t->net, t->account, t->change, t->index, a, sizeof(a));
    CHECK(st == WL_OK && strcmp(a, t->addr) == 0, "%s: got %s (st %d)", t->addr, a, st);
  }
}

static void test_differential(void) {
  g_test = "differential";
  uint8_t seed[64];
  int last_m = -1, last_p = -1, per_scheme[WL_SCHEME_COUNT] = {0};
  for (size_t v = 0; v < N(DIFF_VECTORS); v++) {
    const diff_vec_t *t = &DIFF_VECTORS[v];
    if (t->mnemonic != last_m || t->passphrase != last_p) {
      seed_for(t->mnemonic, t->passphrase, seed);
      last_m = t->mnemonic;
      last_p = t->passphrase;
    }
    char a[WL_ADDRESS_MAX_LEN], ps[64];
    wl_status_t st = wl_address(seed, t->scheme, t->net, t->account, t->change, t->index, a, sizeof(a));
    CHECK(st == WL_OK && strcmp(a, t->addr) == 0, "%s %s: got %s want %s", wl_scheme_info(t->scheme)->name,
          t->path, a, t->addr);
    if (t->scheme != WL_SCHEME_SOL_CLI_SEED) {
      uint32_t path[WL_MAX_PATH_DEPTH];
      size_t d;
      wl_scheme_path(t->scheme, t->net, t->account, t->change, t->index, path, &d);
      wl_path_to_string(path, d, ps, sizeof(ps));
      CHECK(strcmp(ps, t->path) == 0, "path %s vs %s", ps, t->path);
    }
    per_scheme[t->scheme]++;
  }
  for (int s = 0; s < WL_SCHEME_COUNT; s++) CHECK(per_scheme[s] > 0, "scheme %d covered", s);
  wl_wipe(seed, sizeof(seed));
}

static void test_account_keys(void) {
  g_test = "account_keys";
  uint8_t seed[64];
  for (size_t v = 0; v < N(ACCOUNT_KEYS); v++) {
    const acct_vec_t *t = &ACCOUNT_KEYS[v];
    seed_for(t->mnemonic, t->passphrase, seed);
    wl_account_key_t k;
    char pk[67], cc[65];
    CHECK(wl_account_key(seed, t->scheme, t->net, t->account, &k) == WL_OK, "account key %zu", v);
    tohex(k.public_key, 33, pk);
    tohex(k.chain_code, 32, cc);
    CHECK(strcmp(pk, t->pub_hex) == 0 && strcmp(cc, t->chain_hex) == 0, "vec %zu pub/chain", v);
    CHECK(k.master_fingerprint == t->master_fp, "vec %zu fingerprint %08lx vs %08lx", v,
          (unsigned long)k.master_fingerprint, (unsigned long)t->master_fp);
  }
  wl_account_key_t k;
  CHECK(wl_account_key(seed, WL_SCHEME_EVM_STANDARD, WL_NET_MAIN, 0, &k) == WL_ERR_ARG, "non-BTC rejected");
  wl_wipe(seed, sizeof(seed));
}

static void test_scheme_rules(void) {
  g_test = "scheme_rules";
  uint8_t seed[64] = {1};
  char a[WL_ADDRESS_MAX_LEN];
  uint32_t p[WL_MAX_PATH_DEPTH];
  size_t d;
  CHECK(wl_address(seed, WL_SCHEME_EVM_STANDARD, WL_NET_MAIN, 0, 0, 1, a, sizeof(a)) == WL_ERR_ARG, "EVM index!=0");
  CHECK(wl_address(seed, WL_SCHEME_SOL_BIP44_ROOT, WL_NET_MAIN, 1, 0, 0, a, sizeof(a)) == WL_ERR_ARG, "root acct 1");
  CHECK(wl_address(seed, WL_SCHEME_BTC_NATIVE, WL_NET_MAIN, 0, 2, 0, a, sizeof(a)) == WL_ERR_ARG, "change 2");
  CHECK(wl_address(seed, WL_SCHEME_BTC_NATIVE, WL_NET_MAIN, 0x80000000u, 0, 0, a, sizeof(a)) == WL_ERR_ARG, "acct >= 2^31");
  CHECK(wl_address(seed, WL_SCHEME_COUNT, WL_NET_MAIN, 0, 0, 0, a, sizeof(a)) == WL_ERR_ARG, "bad scheme");
  CHECK(wl_address(seed, WL_SCHEME_BTC_TAPROOT, WL_NET_MAIN, 0, 0, 0, a, 20) == WL_ERR_BUFFER && a[0] == 0, "small buffer");
  CHECK(wl_scheme_path(WL_SCHEME_SOL_STANDARD, WL_NET_MAIN, 3, 0, 0, p, &d) == WL_OK && d == 4 &&
            p[3] == WL_HARDENED, "solana path is fully hardened");
  CHECK(wl_scheme_path(WL_SCHEME_BTC_TAPROOT, WL_NET_TEST, 0, 0, 0, p, &d) == WL_OK && p[1] == (WL_HARDENED | 1),
        "testnet coin type 1");
  for (int s = 0; s < WL_SCHEME_COUNT; s++) CHECK((int)wl_scheme_info(s)->id == s, "table order %d", s);
}

/* ------------------------------------------------------------------ entropy */

static void fill_trng(uint8_t *b, size_t n) {
  for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(i * 37 + 11);
}

static void test_entropy(void) {
  g_test = "entropy";
  wl_entropy_t e;
  uint8_t trng[512], se[32], out[64], kat[64];
  char hex[129];
  fill_trng(trng, sizeof(trng));
  for (int i = 0; i < 32; i++) se[i] = (uint8_t)(100 + i);

  /* Known answer (construction documented in entropy.h) */
  wl_entropy_init(&e);
  CHECK(wl_entropy_add_se(&e, se, 32, 256) == WL_OK, "add se");
  CHECK(wl_entropy_add_trng(&e, trng, sizeof(trng)) == WL_OK, "add trng");
  wl_entropy_add_extra(&e, (const uint8_t *)ENTROPY_KAT_EXTRA, strlen(ENTROPY_KAT_EXTRA));
  CHECK(wl_entropy_extract(&e, out, 64) == WL_OK, "extract");
  unhex(ENTROPY_KAT_HEX, kat, 64);
  tohex(out, 64, hex);
  CHECK(memcmp(out, kat, 64) == 0, "KAT with extra: %s", hex);
  CHECK(all_zero(&e, sizeof(e)), "pool wiped after extract");
  CHECK(wl_entropy_extract(&e, out, 32) != WL_OK, "no second extraction from a wiped pool");

  wl_entropy_init(&e);
  wl_entropy_add_trng(&e, trng, sizeof(trng));
  wl_entropy_add_se(&e, se, 32, 256);
  CHECK(wl_entropy_extract(&e, out, 32) == WL_OK, "extract (order independent)");
  unhex(ENTROPY_KAT_NOEXTRA_HEX, kat, 64);
  CHECK(memcmp(out, kat, 32) == 0, "KAT without extra, truncated to 32");

  /* Both sources required */
  wl_entropy_init(&e);
  wl_entropy_add_trng(&e, trng, sizeof(trng));
  CHECK(wl_entropy_extract(&e, out, 32) == WL_ERR_ENTROPY_LOW, "TRNG only -> refused");
  wl_entropy_init(&e);
  wl_entropy_add_se(&e, se, 32, 256);
  CHECK(wl_entropy_extract(&e, out, 32) == WL_ERR_ENTROPY_LOW, "SE only -> refused");
  wl_entropy_init(&e);
  wl_entropy_add_trng(&e, trng, 200); /* 200 bits credited */
  wl_entropy_add_se(&e, se, 32, 256);
  CHECK(wl_entropy_extract(&e, out, 32) == WL_ERR_ENTROPY_LOW, "TRNG 200 bits -> refused");
  wl_entropy_init(&e);
  CHECK(wl_entropy_add_se(&e, se, 32, 257) == WL_ERR_ARG, "SE over-credit rejected");

  /* Health tests latch */
  uint8_t stuck[64];
  memset(stuck, 0xA5, sizeof(stuck));
  wl_entropy_init(&e);
  CHECK(wl_entropy_add_trng(&e, stuck, sizeof(stuck)) == WL_ERR_ENTROPY_HEALTH, "stuck TRNG -> RCT failure");
  CHECK(wl_entropy_add_trng(&e, trng, sizeof(trng)) == WL_ERR_ENTROPY_HEALTH, "latched after failure");
  CHECK(wl_entropy_extract(&e, out, 32) == WL_ERR_ENTROPY_HEALTH, "no output after failure");
  uint8_t biased[512];
  for (size_t i = 0; i < sizeof(biased); i++) biased[i] = (i % 5) ? 0x00 : (uint8_t)i; /* 80% zeros */
  wl_entropy_init(&e);
  CHECK(wl_entropy_add_trng(&e, biased, sizeof(biased)) == WL_ERR_ENTROPY_HEALTH, "biased TRNG -> APT failure");
  memset(stuck, 0x42, 32);
  wl_entropy_init(&e);
  CHECK(wl_entropy_add_se(&e, stuck, 32, 256) == WL_ERR_ENTROPY_HEALTH, "constant SE output rejected");

  /* Any change in either source changes the output */
  uint8_t a[32], b[32];
  wl_entropy_init(&e);
  wl_entropy_add_trng(&e, trng, sizeof(trng));
  wl_entropy_add_se(&e, se, 32, 256);
  wl_entropy_extract(&e, a, 32);
  se[31] ^= 1;
  wl_entropy_init(&e);
  wl_entropy_add_trng(&e, trng, sizeof(trng));
  wl_entropy_add_se(&e, se, 32, 256);
  wl_entropy_extract(&e, b, 32);
  CHECK(memcmp(a, b, 32) != 0, "one SE bit flips the output");
}

/* ------------------------------------------------------------------ hygiene */

static void test_secure(void) {
  g_test = "secure";
  uint8_t buf[100];
  memset(buf, 0xFF, sizeof(buf));
  wl_wipe(buf, sizeof(buf));
  CHECK(all_zero(buf, sizeof(buf)), "wipe");
  uint8_t x[16] = {1, 2, 3}, y[16] = {1, 2, 3};
  CHECK(wl_ct_equal(x, y, 16), "equal");
  y[15] = 1;
  CHECK(!wl_ct_equal(x, y, 16), "not equal");
  /* the address path must leave the output empty on error, never half-written */
  char a[8] = "garbage";
  uint8_t seed[64] = {0};
  wl_address(seed, WL_SCHEME_BTC_NATIVE, WL_NET_MAIN, 0, 0, 0, a, sizeof(a));
  CHECK(a[0] == 0, "error output cleared");
}

int main(void) {
  test_secure();
  test_bip39_official();
  test_bip39_errors();
  test_wordlist();
  test_slip10_ed25519();
  test_official_addresses();
  test_differential();
  test_account_keys();
  test_scheme_rules();
  test_entropy();
  printf("%d checks, %d failed (%zu BIP39, %zu SLIP-10, %zu official address, %zu differential vectors)\n", g_checks,
         g_fail, N(BIP39_VECTORS), N(SLIP10_ED25519), N(OFFICIAL_ADDRESSES), N(DIFF_VECTORS));
  return g_fail ? 1 : 0;
}
