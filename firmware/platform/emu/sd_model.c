/*
 * microSD card (app/sd.h) for the emulator: a 64 MB card image, "sdcard.img" in the state
 * directory (kept in memory with --ephemeral). Created and formatted exFAT on first start (the
 * emulator build has the FatFs formatter; the device firmware does not). "sd out" / "sd in" on
 * the debug link remove and insert the card.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/sd.h"
#include "app/sdfs.h"
#include "emu.h"
#include "ff.h"

#define SD_FILE "sdcard.img"
#define BLOCKS (64u * 2048u) /* 64 MB */

sdfs_status_t sdfs_test_format(uint8_t fmt); /* app/fs/sdfs.c, FF_USE_MKFS builds only */

static uint8_t *img;
static bool present = true, open_, dirty;

void sd_model_load(void) {
  img = calloc(BLOCKS, SD_BLOCK);
  if (!img) return;
  if (!emu_opts.ephemeral && emu_read_file(SD_FILE, img, (size_t)BLOCKS * SD_BLOCK)) return;
  memset(img, 0, (size_t)BLOCKS * SD_BLOCK);
  sdfs_test_format(FM_EXFAT); /* through sd_open/sd_write below, MBR + exFAT like a new card */
  dirty = true;
  if (!emu_opts.ephemeral) emu_write_file(SD_FILE, img, (size_t)BLOCKS * SD_BLOCK), dirty = false;
}

void sd_model_insert(bool in) {
  present = in;
  if (!in) open_ = false; /* pulled out while in use */
}
bool sd_model_present(void) { return present; }

sd_status_t sd_open(sd_info_t *info) {
  memset(info, 0, sizeof(*info));
  if (!present || !img) return SD_NO_CARD;
  info->sdhc = true;
  info->blocks = BLOCKS;
  info->mid = 0x03;
  memcpy(info->oem, "SD", 3);
  memcpy(info->name, "EMU64", 6);
  info->serial = 0x12345678u;
  open_ = true;
  return SD_OK;
}

void sd_close(void) {
  open_ = false;
  if (dirty && !emu_opts.ephemeral) emu_write_file(SD_FILE, img, (size_t)BLOCKS * SD_BLOCK);
  dirty = false;
}

sd_status_t sd_read(uint32_t block, uint8_t out[SD_BLOCK]) {
  if (!open_ || !present) return SD_NO_CARD;
  if (block >= BLOCKS) return SD_RANGE;
  memcpy(out, img + (size_t)block * SD_BLOCK, SD_BLOCK);
  return SD_OK;
}

sd_status_t sd_write(uint32_t block, const uint8_t data[SD_BLOCK]) {
  if (!open_ || !present) return SD_NO_CARD;
  if (block >= BLOCKS) return SD_RANGE;
  memcpy(img + (size_t)block * SD_BLOCK, data, SD_BLOCK);
  dirty = true;
  return SD_OK;
}

void sd_set_clock(uint32_t hz) { (void)hz; }

const char *sd_status_name(sd_status_t s) { return s == SD_OK ? "ok" : s == SD_NO_CARD ? "no card" : "error"; }
