/*
 * RP2350 HAL, second part: battery, TRNG, storage flash, fault handler.
 * (Time, buttons, display, log: hal_display.c. Phone link, idle and sleep: hal_ble.c.)
 *
 * TREDDER_DEMO_NO_SE (the breadboard build without the SE050):
 *   - storage "flash" is RAM: nothing survives a power cycle or reset, so no seed is
 *     ever written anywhere without the secure element protecting it;
 *   - the device's Noise identity key and the paired app's key are RAM-only as well
 *     (proto.c), so the phone must pair again after every restart.
 * Without the flag the build refuses to compile until the real drivers exist.
 */
#include <string.h>

#include "app/hal.h"
#include "board_pins.h"
#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "hardware/structs/trng.h"
#include "pico/stdlib.h"
#include "tusb.h"

#ifndef TREDDER_DEMO_NO_SE
#error "Real flash storage and the SE050 driver are not written yet: only TREDDER_DEMO_NO_SE builds exist"
#endif

/* ---------------------------------------------------------------- power */
void hal_power(hal_power_t *out) {
  static bool init;
  if (!init) {
    adc_init();
    adc_gpio_init(PIN_VBAT_ADC); /* also disables the pad's pulls: the reset pull-down would load R10/R11 */
    gpio_init(PIN_CHG_STAT);
    gpio_set_dir(PIN_CHG_STAT, GPIO_IN);
    gpio_pull_up(PIN_CHG_STAT); /* breadboard charger has no STAT pin: reads "not charging" */
    init = true;
  }
  adc_select_input(BOARD_VBAT_ADC_INPUT);
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += adc_read();
  /* mV = raw/4096 * 3300 * divider (uncalibrated: firmware README rule, per-device gain later) */
  uint32_t mv = sum * 3300u / 16u / 4096u * BOARD_VBAT_DIVIDER_NUM / BOARD_VBAT_DIVIDER_DEN;
  out->vbat_mv = mv < 2500u ? 0 : (uint16_t)mv; /* below 2.5 V: no cell connected */
  out->charging = !gpio_get(PIN_CHG_STAT);
  out->usb = tud_mounted();
}

/* ---------------------------------------------------------------- entropy
   Raw ROSC bits from the TRNG with the hardware decorrelators and checks bypassed (the core's
   health tests and conditioning do that job, credited at 1 bit per byte). */
void hal_trng_raw(uint8_t *out, size_t n) {
  trng_hw->trng_debug_control = 0xF; /* bypass VN corrector, CRNGT and autocorrelation: raw samples */
  trng_hw->sample_cnt1 = 32;         /* ROSC cycles between samples: less correlation than 0 */
  trng_hw->rnd_source_enable = 1;
  while (n) {
    trng_hw->rng_icr = 0xF;
    while (!(trng_hw->rng_isr & 1u)) tight_loop_contents(); /* EHR_VALID: 192 new bits */
    for (unsigned i = 0; i < 6 && n; i++) {
      uint32_t w = trng_hw->ehr_data[i]; /* reading the last word restarts sampling */
      for (int b = 0; b < 4 && n; b++, n--) *out++ = (uint8_t)(w >> (8 * b));
    }
  }
}

/* ---------------------------------------------------------------- storage (RAM in the demo) */
#define STORE_SIZE (HAL_FLASH_SECTOR * HAL_STORAGE_SECTORS)
static uint8_t store[STORE_SIZE];
static bool store_init;

static void store_check(void) {
  if (!store_init) memset(store, 0xFF, sizeof(store)), store_init = true;
}

bool hal_flash_read(uint32_t off, void *buf, size_t n) {
  store_check();
  if (off > STORE_SIZE || n > STORE_SIZE - off) return false;
  memcpy(buf, store + off, n);
  return true;
}

bool hal_flash_erase(uint32_t sector) {
  store_check();
  if (sector >= HAL_STORAGE_SECTORS) return false;
  memset(store + sector * HAL_FLASH_SECTOR, 0xFF, HAL_FLASH_SECTOR);
  return true;
}

bool hal_flash_program(uint32_t off, const void *buf, size_t n) {
  store_check();
  if (off > STORE_SIZE || n > STORE_SIZE - off) return false;
  const uint8_t *d = buf;
  for (size_t i = 0; i < n; i++) store[off + i] &= d[i]; /* NOR semantics, as the real flash */
  return true;
}

/* trezor-crypto calls this if a fault-injection check trips: wipe RAM-held secrets by resetting. */
void tc_fault_handler(const char *msg) { panic("crypto fault: %s", msg); }
