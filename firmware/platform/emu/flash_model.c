/*
 * QSPI NOR flash storage region: erase sets a 4 KiB sector to 0xFF, program
 * can only clear bits (new = old & data), as on the real W25Q chip. Persisted
 * to flash.bin in the state directory.
 */
#include <string.h>

#include "app/hal.h"
#include "emu.h"

#define FLASH_FILE "flash.bin"
#define SIZE (HAL_FLASH_SECTOR * HAL_STORAGE_SECTORS)

static uint8_t mem[SIZE];

static void save(void) {
  if (!emu_opts.ephemeral) emu_write_file(FLASH_FILE, mem, sizeof(mem));
}

void flash_model_load(void) {
  memset(mem, 0xFF, sizeof(mem));
  if (!emu_opts.ephemeral && !emu_read_file(FLASH_FILE, mem, sizeof(mem))) memset(mem, 0xFF, sizeof(mem));
}

bool hal_flash_read(uint32_t off, void *buf, size_t n) {
  if (off > SIZE || n > SIZE - off) return false;
  memcpy(buf, mem + off, n);
  return true;
}

bool hal_flash_erase(uint32_t sector) {
  if (sector >= HAL_STORAGE_SECTORS) return false;
  memset(mem + sector * HAL_FLASH_SECTOR, 0xFF, HAL_FLASH_SECTOR);
  save();
  return true;
}

bool hal_flash_program(uint32_t off, const void *buf, size_t n) {
  if (off > SIZE || n > SIZE - off) return false;
  const uint8_t *d = buf;
  for (size_t i = 0; i < n; i++) mem[off + i] &= d[i];
  save();
  return true;
}
