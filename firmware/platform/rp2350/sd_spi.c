/*
 * microSD in SPI mode on SPI0 (app/sd.h). Physical Layer Simplified Spec v9, section 7.
 *
 *  - CRC is switched ON (CMD59) right after initialisation: every command carries a CRC7 and
 *    every data block a CRC16, checked both ways. A loose breadboard wire or a flaky card then
 *    shows up as SD_IO instead of silently corrupt data (a backup must never be half-right).
 *  - Hardware rules from hardware/README.md section 9: before power-off the SPI pins go low
 *    (a high pin back-powers the card to 2.6 V); after power-off wait >= 150 ms before
 *    powering it again (the card only resets once SD_VDD < 0.5 V).
 *  - Card detect (GP6) is advisory: the breadboard module has none, so a missing card is
 *    detected by CMD0 getting no answer.
 */
#include <string.h>

#include "app/sd.h"
#include "board_pins.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"

#define SD_INIT_HZ (400u * 1000u)
#define POWER_OFF_MIN_MS 150u

static struct {
  bool open, powered_once;
  bool sdhc;
  uint32_t blocks;
  absolute_time_t off_at;
} C;

/* ---------------------------------------------------------------- CRCs */
static uint8_t crc7(const uint8_t *d, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) {
    uint8_t b = d[i];
    for (int k = 0; k < 8; k++, b <<= 1) {
      c <<= 1;
      if ((b ^ c) & 0x80) c ^= 0x09;
    }
  }
  return (uint8_t)(c << 1 | 1); /* 7-bit CRC + end bit */
}

static uint16_t crc16(const uint8_t *d, size_t n) { /* CCITT, poly 0x1021, init 0 */
  uint16_t c = 0;
  for (size_t i = 0; i < n; i++) {
    c ^= (uint16_t)d[i] << 8;
    for (int k = 0; k < 8; k++) c = c & 0x8000 ? (uint16_t)(c << 1 ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}

/* ---------------------------------------------------------------- SPI bytes */
static uint8_t xfer(uint8_t b) {
  uint8_t r;
  spi_write_read_blocking(BOARD_SD_SPI, &b, &r, 1);
  return r;
}
static void cs(bool sel) { gpio_put(PIN_SD_CS, !sel); }

static void pins_spi(void) {
  gpio_set_function(PIN_SD_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SD_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SD_MISO, GPIO_FUNC_SPI);
  gpio_pull_up(PIN_SD_MISO); /* DO floats until the card drives it */
  gpio_init(PIN_SD_CS);
  gpio_put(PIN_SD_CS, 1);
  gpio_set_dir(PIN_SD_CS, GPIO_OUT);
}

static void pins_low(void) {
  const uint pins[] = {PIN_SD_MISO, PIN_SD_CS, PIN_SD_SCK, PIN_SD_MOSI};
  for (unsigned i = 0; i < 4; i++) {
    gpio_init(pins[i]);
    gpio_disable_pulls(pins[i]);
    gpio_put(pins[i], 0);
    gpio_set_dir(pins[i], GPIO_OUT);
  }
}

/* Wait until the card stops signalling busy (MISO held low). */
static bool wait_ready(uint32_t ms) {
  absolute_time_t end = make_timeout_time_ms(ms);
  do {
    if (xfer(0xFF) == 0xFF) return true;
  } while (absolute_time_diff_us(get_absolute_time(), end) > 0);
  return false;
}

/* Send a command, return R1 (0xFF = no answer). Leaves CS low: the caller reads the rest. */
static uint8_t cmd(uint8_t index, uint32_t arg) {
  uint8_t f[6] = {(uint8_t)(0x40 | index), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16), (uint8_t)(arg >> 8), (uint8_t)arg, 0};
  f[5] = crc7(f, 5);
  cs(false);
  xfer(0xFF);
  cs(true);
  if (index != 0 && !wait_ready(500)) return 0xFF;
  spi_write_blocking(BOARD_SD_SPI, f, 6);
  if (index == 12) xfer(0xFF); /* CMD12: skip the stuff byte */
  uint8_t r = 0xFF;
  for (int i = 0; i < 10 && (r & 0x80); i++) r = xfer(0xFF);
  return r;
}

static void deselect(void) {
  cs(false);
  xfer(0xFF); /* the card releases MISO after one more clock byte */
}

static uint8_t acmd(uint8_t index, uint32_t arg) {
  uint8_t r = cmd(55, 0);
  if (r > 1) return r;
  deselect();
  return cmd(index, arg);
}

/* Read a data block (after a read command): token, n bytes, CRC16 checked. */
static bool read_data(uint8_t *out, size_t n) {
  uint8_t tok = 0xFF;
  absolute_time_t end = make_timeout_time_ms(200);
  while ((tok = xfer(0xFF)) == 0xFF && absolute_time_diff_us(get_absolute_time(), end) > 0) {
  }
  if (tok != 0xFE) return false; /* error token or timeout */
  memset(out, 0xFF, n);
  spi_write_read_blocking(BOARD_SD_SPI, out, out, n); /* sends 0xFF while reading */
  uint16_t got = (uint16_t)(xfer(0xFF) << 8);
  got |= xfer(0xFF);
  return got == crc16(out, n);
}

/* ---------------------------------------------------------------- power */
static void power(bool on) {
  if (on) {
    if (C.powered_once) { /* hardware rule 2: let SD_VDD fall below 0.5 V first */
      int64_t since = absolute_time_diff_us(C.off_at, get_absolute_time()) / 1000;
      if (since < POWER_OFF_MIN_MS) sleep_ms((uint32_t)(POWER_OFF_MIN_MS - since));
    }
    gpio_init(PIN_SD_PWR_N);
    gpio_put(PIN_SD_PWR_N, 0); /* Q3 on (the breadboard card is always powered) */
    gpio_set_dir(PIN_SD_PWR_N, GPIO_OUT);
    sleep_ms(5); /* supply ramp (C8: 0.7 ms) + the card's own power-up time */
    C.powered_once = true;
  } else {
    pins_low(); /* hardware rule 1 */
    gpio_init(PIN_SD_PWR_N);
    gpio_put(PIN_SD_PWR_N, 1);
    gpio_set_dir(PIN_SD_PWR_N, GPIO_OUT);
    C.off_at = get_absolute_time();
  }
}

void sd_close(void) {
  if (C.open || C.powered_once) {
    spi_deinit(BOARD_SD_SPI);
    power(false);
  }
  C.open = false;
}

/* ---------------------------------------------------------------- open */
static uint32_t bits(const uint8_t *r, int msb, int lsb) { /* bit range of a 128-bit register */
  uint32_t v = 0;
  for (int b = msb; b >= lsb; b--) v = v << 1 | ((r[15 - b / 8] >> (b % 8)) & 1u);
  return v;
}

sd_status_t sd_open(sd_info_t *info) {
  sd_close();
  memset(info, 0, sizeof(*info));
  power(true);
  spi_init(BOARD_SD_SPI, SD_INIT_HZ);
  spi_set_format(BOARD_SD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
  pins_spi();

  /* >= 74 clocks with CS high, then CMD0 until the card is idle */
  for (int i = 0; i < 10; i++) xfer(0xFF);
  uint8_t r = 0xFF;
  for (int i = 0; i < 10 && r != 0x01; i++) {
    r = cmd(0, 0);
    deselect();
  }
  if (r != 0x01) {
    sd_close();
    return SD_NO_CARD;
  }

  /* CMD8: v2 card? (voltage 2.7-3.6 V, check pattern 0xAA) */
  bool v2 = false;
  r = cmd(8, 0x1AA);
  if (r == 0x01) {
    uint8_t r7[4];
    for (int i = 0; i < 4; i++) r7[i] = xfer(0xFF);
    v2 = true;
    if ((r7[2] & 0x0F) != 0x01 || r7[3] != 0xAA) r = 0xFF; /* voltage not accepted */
  }
  deselect();
  if (r == 0xFF) {
    sd_close();
    return SD_UNUSABLE;
  }

  /* ACMD41 until ready (up to 1 s), asking for high capacity on v2 cards */
  absolute_time_t end = make_timeout_time_ms(1000);
  do {
    r = acmd(41, v2 ? 0x40000000u : 0);
    deselect();
  } while (r == 0x01 && absolute_time_diff_us(get_absolute_time(), end) > 0);
  if (r != 0x00) {
    sd_close();
    return SD_UNUSABLE;
  }

  /* CMD58: CCS bit = block addressing */
  C.sdhc = false;
  if (v2) {
    uint8_t ocr[4] = {0};
    r = cmd(58, 0);
    for (int i = 0; i < 4; i++) ocr[i] = xfer(0xFF);
    deselect();
    if (r != 0x00) {
      sd_close();
      return SD_IO;
    }
    C.sdhc = ocr[0] & 0x40;
  }
  if (!C.sdhc) {
    r = cmd(16, SD_BLOCK); /* SDSC: fix the block length at 512 */
    deselect();
    if (r != 0x00) {
      sd_close();
      return SD_UNUSABLE;
    }
  }

  r = cmd(59, 1); /* CRC on */
  deselect();
  if (r != 0x00) {
    sd_close();
    return SD_UNUSABLE;
  }

  spi_set_baudrate(BOARD_SD_SPI, SD_FAST_HZ);

  /* CSD -> capacity, CID -> identity (both CRC-checked) */
  uint8_t csd[16];
  r = cmd(9, 0);
  bool ok = r == 0x00 && read_data(csd, 16);
  deselect();
  if (ok) {
    r = cmd(10, 0);
    ok = r == 0x00 && read_data(info->cid, 16);
    deselect();
  }
  if (!ok) {
    sd_close();
    return SD_IO;
  }
  if ((csd[0] >> 6) == 1) { /* CSD v2: (C_SIZE + 1) * 512 KiB */
    C.blocks = (bits(csd, 69, 48) + 1u) * 1024u;
  } else { /* CSD v1 */
    uint32_t c_size = bits(csd, 73, 62), mult = bits(csd, 49, 47), bl_len = bits(csd, 83, 80);
    C.blocks = (c_size + 1u) << (mult + 2u + bl_len - 9u);
  }
  info->sdhc = C.sdhc;
  info->blocks = C.blocks;
  info->mid = info->cid[0];
  memcpy(info->oem, &info->cid[1], 2);
  memcpy(info->name, &info->cid[3], 5);
  info->serial = (uint32_t)info->cid[9] << 24 | (uint32_t)info->cid[10] << 16 | (uint32_t)info->cid[11] << 8 | info->cid[12];
  C.open = true;
  return SD_OK;
}

/* ---------------------------------------------------------------- blocks */
sd_status_t sd_read(uint32_t block, uint8_t out[SD_BLOCK]) {
  if (!C.open) return SD_NO_CARD;
  if (block >= C.blocks) return SD_RANGE;
  for (int attempt = 0; attempt < 2; attempt++) { /* one retry: a CRC error is usually a glitch */
    uint8_t r = cmd(17, C.sdhc ? block : block * SD_BLOCK);
    bool ok = r == 0x00 && read_data(out, SD_BLOCK);
    deselect();
    if (ok) return SD_OK;
  }
  memset(out, 0, SD_BLOCK);
  return SD_IO;
}

sd_status_t sd_write(uint32_t block, const uint8_t data[SD_BLOCK]) {
  if (!C.open) return SD_NO_CARD;
  if (block >= C.blocks) return SD_RANGE;
  uint8_t r = cmd(24, C.sdhc ? block : block * SD_BLOCK);
  if (r != 0x00) {
    deselect();
    return SD_IO;
  }
  uint16_t c = crc16(data, SD_BLOCK);
  xfer(0xFF);
  xfer(0xFE); /* start token */
  spi_write_blocking(BOARD_SD_SPI, data, SD_BLOCK);
  xfer((uint8_t)(c >> 8));
  xfer((uint8_t)c);
  uint8_t resp = xfer(0xFF) & 0x1F; /* data response: 0x05 accepted, 0x0B CRC error, 0x0D write error */
  bool ok = resp == 0x05 && wait_ready(500);
  deselect();
  if (!ok) return SD_IO;
  r = cmd(13, 0); /* SEND_STATUS: R2, both bytes must be 0 */
  uint8_t r2 = xfer(0xFF);
  deselect();
  return r == 0x00 && r2 == 0x00 ? SD_OK : SD_IO;
}

void sd_set_clock(uint32_t hz) {
  if (C.open) spi_set_baudrate(BOARD_SD_SPI, hz);
}

const char *sd_status_name(sd_status_t s) {
  switch (s) {
    case SD_OK: return "ok";
    case SD_NO_CARD: return "no card (nothing answered)";
    case SD_UNUSABLE: return "card refused to initialise";
    case SD_IO: return "I/O or CRC error";
    case SD_RANGE: return "block out of range";
  }
  return "?";
}
