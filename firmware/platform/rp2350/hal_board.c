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

int hal_vbus(void); /* hal_ble.c */

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
  /* Is anything connected to GP28? Read once with the pad's pull-up, once with its pull-down. The
     R10/R11 divider (2.35 kohm source) barely moves under a ~50 kohm pull; a floating pin (the
     breadboard built without battery, divider and diode) follows the pull. Without this check a
     floating pin could read as a low battery and send a device on a wall charger to sleep. */
  uint32_t up = 0, down = 0;
  gpio_pull_up(PIN_VBAT_ADC);
  sleep_us(200);
  for (int i = 0; i < 8; i++) up += adc_read();
  gpio_pull_down(PIN_VBAT_ADC);
  sleep_us(200);
  for (int i = 0; i < 8; i++) down += adc_read();
  gpio_disable_pulls(PIN_VBAT_ADC);
  bool floating = up > down + 8u * 600u; /* > ~0.5 V apart (raw 600 of 4096) */
  uint32_t sum = up + down;              /* the two pulls' small errors cancel */
  /* mV = raw/4096 * 3300 * divider (uncalibrated: firmware README rule, per-device gain later) */
  uint32_t mv = sum * 3300u / 16u / 4096u * BOARD_VBAT_DIVIDER_NUM / BOARD_VBAT_DIVIDER_DEN;
  /* A 1S LiPo never exceeds 4.2 V: more means GP28 sees the whole battery voltage (R11, the lower half
     of the divider, missing) or the ADC is at full scale. Treat as "no reading", say why. */
  bool implausible = !floating && mv > 4600u;
  out->vbat_mv = floating || implausible || mv < 2500u ? 0 : (uint16_t)mv; /* 0 = no cell connected */
  out->vbat_state = floating ? HAL_VBAT_NOT_CONNECTED : implausible ? HAL_VBAT_TOO_HIGH : mv < 2500u ? HAL_VBAT_TOO_LOW : HAL_VBAT_OK;
  out->raw_mv = (uint16_t)(mv > 65535u ? 65535u : mv);
  static int8_t had = -1; /* log only when the state changes */
  int8_t now = out->vbat_mv ? 1 : implausible ? 2 : 0;
  if (had != now) {
    had = now;
    if (now == 1) hal_log("battery: %u mV", out->vbat_mv);
    else if (now == 2) hal_log("battery: reading %lu mV is impossible: check the divider (R11 missing? GP28 must get HALF the battery voltage)", (unsigned long)mv);
    else hal_log("battery: none (GP28 %s)", floating ? "not connected" : "below 2.5 V");
  }
  int vbus = hal_vbus();
  out->usb = vbus < 0 ? tud_mounted() : vbus == 1;
  /* PCB: the MCP73831's STAT pin is low while charging. Breadboard: the charger has no STAT pin
     (reads high through the pull-up), so: USB power present and the cell not yet full. */
  out->charging = !gpio_get(PIN_CHG_STAT) || (out->usb && out->vbat_mv && out->vbat_mv < 4150u);
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
