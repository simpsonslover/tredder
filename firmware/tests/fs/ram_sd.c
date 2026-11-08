/* app/sd.h on a RAM image, for tools/fs_test.py. */
#include <stdlib.h>
#include <string.h>

#include "app/sd.h"
#include "ram_sd.h"

uint8_t *ram_img;
uint32_t ram_blocks;
bool ram_present = true, ram_readonly, ram_open;
unsigned long ram_reads, ram_writes;

void ram_sd_create(uint32_t blocks) {
  free(ram_img);
  ram_img = calloc(blocks, SD_BLOCK);
  ram_blocks = blocks;
  ram_present = true;
  ram_readonly = ram_open = false;
}

sd_status_t sd_open(sd_info_t *info) {
  memset(info, 0, sizeof(*info));
  if (!ram_present || !ram_img) return SD_NO_CARD;
  info->sdhc = true;
  info->blocks = ram_blocks;
  memcpy(info->name, "RAMSD", 6);
  ram_open = true;
  return SD_OK;
}

void sd_close(void) { ram_open = false; }

sd_status_t sd_read(uint32_t block, uint8_t out[SD_BLOCK]) {
  if (!ram_open) return SD_NO_CARD;
  if (block >= ram_blocks) return SD_RANGE;
  memcpy(out, ram_img + (size_t)block * SD_BLOCK, SD_BLOCK);
  ram_reads++;
  return SD_OK;
}

sd_status_t sd_write(uint32_t block, const uint8_t data[SD_BLOCK]) {
  if (!ram_open) return SD_NO_CARD;
  if (block >= ram_blocks) return SD_RANGE;
  if (ram_readonly) return SD_IO;
  memcpy(ram_img + (size_t)block * SD_BLOCK, data, SD_BLOCK);
  ram_writes++;
  return SD_OK;
}

const char *sd_status_name(sd_status_t s) { return s == SD_OK ? "ok" : "error"; }
