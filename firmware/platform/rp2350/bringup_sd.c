/*
 * Bring-up test: microSD card (breadboard or PCB). READ-ONLY: never writes to the card.
 *   - identity (CID) and capacity (CSD), CRC-checked
 *   - partition table (MBR / none / GPT)
 *   - read speed over 1 MiB, and a power cycle + re-open (hardware rules 1 and 2)
 *   - mounts the filesystem (FAT32 or exFAT, app/fs) and lists the files in the root folder
 * Results on the display and the USB serial console.
 */
#include <stdio.h>
#include <string.h>

#include "app/gfx.h"
#include "app/sd.h"
#include "app/sdfs.h"
#include "board_pins.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#define BG RGB565(48, 48, 48)
#define BAR RGB565(66, 66, 66)
#define FG RGB565(255, 255, 255)
#define ACC RGB565(129, 212, 250)
#define OK RGB565(128, 255, 128)
#define BAD RGB565(255, 128, 128)

static int y;
static void title(const char *t) {
  gfx_fill(0, 0, GFX_W, GFX_H, BG);
  gfx_fill(0, 0, GFX_W, 30, BAR);
  gfx_text(10, 5, t, &wl_font_title, FG);
  y = 40;
}
static void kv(const char *k, const char *v, uint16_t c) {
  gfx_text(10, y, k, &wl_font_body, ACC);
  gfx_text(110, y, v, &wl_font_body, c);
  y += 18;
  printf("  %-10s %s\n", k, v);
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

typedef struct {
  int shown, total;
} files_t;

static bool file_cb(const char *name, uint64_t size, bool is_dir, void *ctx) {
  files_t *f = ctx;
  char line[64];
  if (is_dir) snprintf(line, sizeof(line), "[%.40s]", name);
  else snprintf(line, sizeof(line), "%.36s  %lu KB", name, (unsigned long)((size + 1023) / 1024));
  printf("    %s\n", line);
  if (f->shown < 2) gfx_text(20, y, line, &wl_font_body, FG), y += 18, f->shown++;
  f->total++;
  return f->total < 200;
}

/* No answer: look at the MISO wire directly and try CMD0 slowly, printing the raw bytes.
     MISO "follows the pulls"  -> nothing drives it: MISO wire open, or the module has no power
     MISO held high / low      -> the module drives it
     raw reply 01              -> the card answers at 100 kHz (then the problem is speed)
     raw reply all FF          -> the card is silent (power too low, CS/SCK/MOSI wire, card seating) */
static const char *pin_state(uint pin) {
  gpio_pull_down(pin);
  sleep_us(300);
  bool lo = gpio_get(pin);
  gpio_pull_up(pin);
  sleep_us(300);
  bool hi = gpio_get(pin);
  gpio_disable_pulls(pin);
  return !lo && hi ? "follows the pulls (not driven)" : lo && hi ? "held HIGH" : !lo && !hi ? "held LOW" : "odd";
}

static void diagnose(void) {
  char s[64];
  gpio_init(PIN_SD_CS);
  gpio_put(PIN_SD_CS, 1);
  gpio_set_dir(PIN_SD_CS, GPIO_OUT);
  gpio_init(PIN_SD_MISO);
  gpio_set_dir(PIN_SD_MISO, GPIO_IN);
  const char *idle = pin_state(PIN_SD_MISO);
  gpio_put(PIN_SD_CS, 0);
  const char *sel = pin_state(PIN_SD_MISO);
  gpio_put(PIN_SD_CS, 1);
  printf("  MISO, card not selected: %s\n  MISO, card selected:     %s\n", idle, sel);
  gfx_text(10, y, "MISO idle:", &wl_font_body, ACC), gfx_text(110, y, idle, &wl_font_body, FG), y += 18;
  gfx_text(10, y, "MISO sel.:", &wl_font_body, ACC), gfx_text(110, y, sel, &wl_font_body, FG), y += 18;

  spi_init(BOARD_SD_SPI, 100 * 1000);
  spi_set_format(BOARD_SD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  gpio_set_function(PIN_SD_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI);
  gpio_pull_up(PIN_SD_MISO);
  uint8_t ff[20], rx[20];
  memset(ff, 0xFF, sizeof(ff));
  spi_write_blocking(BOARD_SD_SPI, ff, 20); /* 160 clocks with CS high */
  static const uint8_t cmd0[6] = {0x40, 0, 0, 0, 0, 0x95};
  gpio_put(PIN_SD_CS, 0);
  spi_write_blocking(BOARD_SD_SPI, cmd0, 6);
  spi_write_read_blocking(BOARD_SD_SPI, ff, rx, 12);
  gpio_put(PIN_SD_CS, 1);
  spi_write_blocking(BOARD_SD_SPI, ff, 1);
  int n = snprintf(s, sizeof(s), "CMD0 @100k:");
  printf("  raw reply to CMD0 at 100 kHz:");
  for (int i = 0; i < 12; i++) {
    printf(" %02X", rx[i]);
    if (i < 8) n += snprintf(s + n, sizeof(s) - (size_t)n, " %02X", rx[i]);
  }
  printf("\n");
  gfx_text(10, y, s, &wl_font_body, FG), y += 18;
  spi_deinit(BOARD_SD_SPI);
  const uint pins[] = {PIN_SD_MISO, PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI};
  for (unsigned i = 0; i < 4; i++) gpio_init(pins[i]), gpio_disable_pulls(pins[i]), gpio_put(pins[i], 0), gpio_set_dir(pins[i], GPIO_OUT);
}

static int run(void) {
  char s[48];
  sd_info_t info;
  title("microSD test");
  printf("\n== microSD bring-up (read-only) ==\n");
  sd_status_t st = sd_open(&info);
  if (st != SD_OK) {
    kv("Card", sd_status_name(st), BAD);
    printf("FAILED: %s\n", sd_status_name(st));
    diagnose();
    y += 6;
    gfx_text(10, y, "Check: card inserted, VCC/GND,", &wl_font_body, FG);
    gfx_text(10, y + 18, "MISO=pin1 CS=pin2 SCK=pin4 MOSI=pin5.", &wl_font_body, FG);
    gfx_text(10, y + 36, "5V-only module? See BREADBOARD.md", &wl_font_body, FG);
    return -1; /* card not started: main() retries */
  }
  snprintf(s, sizeof(s), "%s %.5s (OEM %.2s)", info.sdhc ? "SDHC/XC" : "SDSC", info.name, info.oem);
  kv("Card", s, OK);
  snprintf(s, sizeof(s), "%lu MB (%lu blocks)", (unsigned long)(info.blocks / 2048u), (unsigned long)info.blocks);
  kv("Capacity", s, FG);
  snprintf(s, sizeof(s), "maker 0x%02X, serial %08lX", info.mid, (unsigned long)info.serial);
  kv("Identity", s, FG);

  static uint8_t b[SD_BLOCK];
  int fails = 0;
  st = sd_read(0, b);
  if (st != SD_OK) {
    kv("Block 0", sd_status_name(st), BAD);
    return 1;
  }
  uint32_t part = 0;
  if (b[510] == 0x55 && b[511] == 0xAA && b[446 + 4] != 0 && b[446 + 4] != 0xEE) { /* MBR */
    part = le32(b + 446 + 8);
    snprintf(s, sizeof(s), "MBR, type 0x%02X at block %lu", b[446 + 4], (unsigned long)part);
  } else if (b[446 + 4] == 0xEE) {
    snprintf(s, sizeof(s), "GPT (not supported for backups)");
  } else {
    snprintf(s, sizeof(s), "no partition table (superfloppy)");
  }
  kv("Partition", s, FG);

  /* speed sweep: 1 MiB (2048 blocks) at each data clock; errors = blocks that failed their CRC
     twice (sd_read retries once). The driver's default SD_FAST_HZ must be error-free. */
  static const uint32_t khz[] = {500, 1000, 2000, 4000, 8000};
  char sweep[48] = "";
  int bad_default = 0;
  printf("  speed sweep (1 MiB each):\n");
  for (unsigned k = 0; k < 5; k++) {
    sd_set_clock(khz[k] * 1000u);
    absolute_time_t t0 = get_absolute_time();
    int bad = 0;
    for (uint32_t i = 0; i < 2048 && i < info.blocks; i++) bad += sd_read(i, b) != SD_OK;
    int64_t us = absolute_time_diff_us(t0, get_absolute_time());
    printf("    %5lu kHz: %4lu KB/s, %d bad blocks\n", (unsigned long)khz[k],
           (unsigned long)(1024ull * 1000000ull / (uint64_t)us), bad);
    size_t n = strlen(sweep);
    snprintf(sweep + n, sizeof(sweep) - n, "%s%d", k ? "/" : "", bad);
    if (khz[k] * 1000u == SD_FAST_HZ) bad_default = bad;
  }
  sd_set_clock(SD_FAST_HZ);
  kv("Bad@.5-8M", sweep, bad_default ? BAD : FG);
  fails += bad_default != 0;

  /* power cycle: close (pins low, power off), reopen (waits >= 150 ms) */
  sd_close();
  sd_info_t again;
  st = sd_open(&again);
  bool same = st == SD_OK && !memcmp(again.cid, info.cid, 16);
  kv("Power cycle", same ? "re-opened, same card" : sd_status_name(st), same ? OK : BAD);
  fails += !same;
  sd_close();

  /* filesystem: mount (read-only use: nothing below writes) */
  sdfs_info_t fi;
  sdfs_status_t fst = sdfs_mount(&fi);
  if (fst != SDFS_OK) {
    kv("Filesystem", sdfs_status_name(fst), BAD);
    printf("  -> format the card as exFAT or FAT32, MBR partition scheme\n");
    fails++;
  } else {
    uint64_t freeb = 0;
    sdfs_free(&freeb);
    snprintf(s, sizeof(s), "%s, %lu MB free", sdfs_type_name(fi.type), (unsigned long)(freeb >> 20));
    kv("Filesystem", s, OK);
    printf("  root folder:\n");
    files_t f = {0};
    sdfs_list("", file_cb, &f);
    if (f.total > f.shown) gfx_text(20, y, "...", &wl_font_body, FG), y += 18;
    if (f.total == 0) gfx_text(20, y, "(no files)", &wl_font_body, FG), y += 18, printf("    (no files)\n");
    sdfs_unmount();
  }

  y += 8;
  gfx_text(10, y, fails ? "Problems found (see above)" : "All checks passed. Nothing was written.", &wl_font_bold, fails ? BAD : OK);
  printf(fails ? "%d problem(s)\n" : "ALL PASS (nothing written to the card)\n", fails);
  return fails;
}

int main(void) {
  stdio_init_all();
  sleep_ms(1500);
  gfx_init();
  /* Until the card starts, retry every 2 s, so wires can be fixed while watching the screen. */
  for (unsigned attempt = 1;; attempt++) {
    int r = run();
    if (r < 0) {
      char s[40];
      snprintf(s, sizeof(s), "attempt %u, retrying every 2 s", attempt);
      gfx_text(10, GFX_H - 22, s, &wl_font_body, ACC);
    }
    gfx_flush();
    if (r >= 0) break;
    sleep_ms(2000);
  }
  for (;;) sleep_ms(1000);
}
