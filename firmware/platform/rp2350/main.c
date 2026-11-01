/*
 * MILESTONE M1 firmware: on-target self-test of the wallet core.
 *
 * Flash it to a bare Pico 2 W (no custom board needed), open the USB serial
 * port, and it checks BIP39, BIP32/SLIP-10 and every address scheme against
 * known vectors ON THE RP2350 ITSELF (catches compiler/architecture issues the
 * host tests can't), then reports timings. Uses only public test phrases;
 * never type a real seed into a development build.
 */
#include <stdio.h>
#include <string.h>

#include "board_pins.h"
#include "pico/stdlib.h"
#include "wallet/address.h"
#include "wallet/mnemonic.h"
#include "wallet/secure.h"

/* Public BIP84/86/44 test phrase and reference addresses (see tests/host/vectors.h). */
static const char *const PHRASE = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
static const struct { wl_scheme_t s; wl_net_t n; uint32_t acct, change, idx; const char *addr; } KAT[] = {
    {WL_SCHEME_BTC_LEGACY, WL_NET_MAIN, 0, 0, 0, "1LqBGSKuX5yYUonjxT5qGfpUsXKYYWeabA"},
    {WL_SCHEME_BTC_NESTED, WL_NET_TEST, 0, 0, 0, "2Mww8dCYPUpKHofjgcXcBCEGmniw9CoaiD2"},
    {WL_SCHEME_BTC_NATIVE, WL_NET_MAIN, 0, 0, 0, "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"},
    {WL_SCHEME_BTC_TAPROOT, WL_NET_MAIN, 0, 0, 0, "bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr"},
    {WL_SCHEME_EVM_STANDARD, WL_NET_MAIN, 0, 0, 0, "0x9858EfFD232B4033E47d90003D41EC34EcaEda94"},
    {WL_SCHEME_SOL_STANDARD, WL_NET_MAIN, 0, 0, 0, "HAgk14JpMQLgt6rVgv7cBQFJWFto5Dqxi472uT3DKpqk"},
};

int main(void) {
  stdio_init_all();
  sleep_ms(2000); /* give the host time to open the USB serial port */
  printf("\n== wallet core self-test (M1) ==\n");

  uint16_t idx[WL_MNEMONIC_MAX_WORDS];
  size_t words = 0, bad = 0;
  uint8_t seed[64];
  int fails = 0;

  absolute_time_t t0 = get_absolute_time();
  if (wl_mnemonic_parse(PHRASE, idx, &words, &bad) != WL_OK ||
      wl_mnemonic_to_seed(idx, words, "", seed) != WL_OK) {
    printf("FAIL: mnemonic\n");
    fails++;
  }
  printf("BIP39 seed (PBKDF2 2048): %lld ms\n", absolute_time_diff_us(t0, get_absolute_time()) / 1000);

  for (size_t i = 0; i < sizeof(KAT) / sizeof(KAT[0]); i++) {
    char a[WL_ADDRESS_MAX_LEN];
    t0 = get_absolute_time();
    wl_status_t st = wl_address(seed, KAT[i].s, KAT[i].n, KAT[i].acct, KAT[i].change, KAT[i].idx, a, sizeof(a));
    int ok = st == WL_OK && strcmp(a, KAT[i].addr) == 0;
    fails += !ok;
    printf("%s %-20s %s (%lld ms)\n", ok ? "PASS" : "FAIL", wl_scheme_info(KAT[i].s)->name, a,
           absolute_time_diff_us(t0, get_absolute_time()) / 1000);
  }
  wl_wipe(seed, sizeof(seed));
  wl_wipe(idx, sizeof(idx));
  if (fails) printf("\n%d FAILURES\n", fails);
  else printf("\nALL PASS\n");

  for (;;) sleep_ms(1000);
}
