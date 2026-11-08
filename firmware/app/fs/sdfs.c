/*
 * Files on the microSD card (app/fs: linked only into builds with an SD driver): FatFs (FAT12/16/32 + exFAT) over the block driver in app/sd.h.
 * See app/sdfs.h for the API and the untrusted-card rules.
 *
 * Security trade-off: exFAT support means running FatFs, a full filesystem parser (~7000 lines),
 * on metadata from a card anyone could have prepared. FatFs is mature and widely deployed, and
 * it only ever sees the card: no key, seed or PIN is in its buffers. A malformed card should at
 * worst give an error (fuzzed on the PC: tools/fs_test.py). Files themselves are authenticated
 * by the backup format (AES-256-GCM), not trusted because FatFs returned them.
 */
#include "app/sdfs.h"

#include <string.h>

#include "ff.h"
#include "diskio.h" /* after ff.h: uses its types */

static struct {
  bool card;     /* sd_open succeeded: the block device is live */
  bool mounted;  /* f_mount succeeded */
  uint32_t blocks;
} M;

static FATFS fs;
static FIL fil;  /* static: holds a 512-byte sector buffer, too big for the stack */

/* ---------------------------------------------------------------- FatFs disk interface (drive 0) */
DSTATUS disk_status(BYTE pdrv) { return pdrv == 0 && M.card ? 0 : STA_NOINIT; }
DSTATUS disk_initialize(BYTE pdrv) { return disk_status(pdrv); } /* the card is opened by sdfs_mount */

static DRESULT range_ok(BYTE pdrv, LBA_t sector, UINT count) {
  if (pdrv != 0 || count == 0) return RES_PARERR;
  if (!M.card) return RES_NOTRDY;
  if (sector >= M.blocks || count > M.blocks - sector) return RES_PARERR;
  return RES_OK;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
  DRESULT r = range_ok(pdrv, sector, count);
  for (UINT i = 0; r == RES_OK && i < count; i++)
    if (sd_read((uint32_t)(sector + i), buff + (size_t)i * SD_BLOCK) != SD_OK) r = RES_ERROR;
  return r;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
  DRESULT r = range_ok(pdrv, sector, count);
  for (UINT i = 0; r == RES_OK && i < count; i++)
    if (sd_write((uint32_t)(sector + i), buff + (size_t)i * SD_BLOCK) != SD_OK) r = RES_ERROR;
  return r;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
  if (pdrv != 0) return RES_PARERR;
  if (!M.card) return RES_NOTRDY;
  switch (cmd) {
    case CTRL_SYNC: return RES_OK; /* sd_write returns only after the card finished programming */
    case GET_SECTOR_COUNT: *(LBA_t *)buff = M.blocks; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = SD_BLOCK; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK; /* erase block unknown */
    default: return RES_PARERR;
  }
}

/* ---------------------------------------------------------------- helpers */
static sdfs_status_t map(FRESULT r) {
  switch (r) {
    case FR_OK: return SDFS_OK;
    case FR_NOT_READY: return SDFS_NO_CARD;
    case FR_NO_FILESYSTEM: return SDFS_NOT_FORMATTED;
    case FR_NO_FILE:
    case FR_NO_PATH: return SDFS_NOT_FOUND;
    case FR_INVALID_NAME: return SDFS_BAD_NAME;
    case FR_DENIED: return SDFS_FULL;
    case FR_WRITE_PROTECTED: return SDFS_WRITE_PROTECTED;
    case FR_INVALID_OBJECT:
    case FR_INVALID_DRIVE:
    case FR_NOT_ENABLED: return SDFS_NOT_MOUNTED;
    default: return SDFS_IO; /* FR_DISK_ERR, FR_INT_ERR (inconsistent structures), ... */
  }
}

static void close_fil(void) {
  f_close(&fil);
  memset(&fil, 0, sizeof(fil)); /* drop the cached sector */
}

/* ---------------------------------------------------------------- API */
sdfs_status_t sdfs_mount(sdfs_info_t *info) {
  sdfs_unmount();
  memset(info, 0, sizeof(*info));
  sd_status_t s = sd_open(&info->card);
  if (s != SD_OK) return s == SD_NO_CARD ? SDFS_NO_CARD : s == SD_UNUSABLE ? SDFS_CARD_ERROR : SDFS_IO;
  M.card = true;
  M.blocks = info->card.blocks;
  FRESULT r = f_mount(&fs, "", 1);
  if (r != FR_OK) {
    sdfs_unmount();
    return map(r);
  }
  M.mounted = true;
  switch (fs.fs_type) {
    case FS_FAT12: info->type = SDFS_FAT12; break;
    case FS_FAT16: info->type = SDFS_FAT16; break;
    case FS_FAT32: info->type = SDFS_FAT32; break;
    default: info->type = SDFS_EXFAT; break;
  }
  info->cluster_bytes = (uint32_t)fs.csize * SD_BLOCK;
  info->volume_bytes = (uint64_t)(fs.n_fatent - 2) * info->cluster_bytes;
  return SDFS_OK;
}

void sdfs_unmount(void) {
  if (M.mounted) f_unmount("");
  memset(&fs, 0, sizeof(fs));
  memset(&fil, 0, sizeof(fil));
  if (M.card) sd_close();
  M.mounted = M.card = false;
}

sdfs_status_t sdfs_free(uint64_t *bytes) {
  *bytes = 0;
  if (!M.mounted) return SDFS_NOT_MOUNTED;
  DWORD clusters;
  FATFS *f;
  FRESULT r = f_getfree("", &clusters, &f);
  if (r == FR_OK) *bytes = (uint64_t)clusters * f->csize * SD_BLOCK;
  return map(r);
}

sdfs_status_t sdfs_read_file(const char *path, uint8_t *buf, size_t max, size_t *n) {
  *n = 0;
  if (!M.mounted) return SDFS_NOT_MOUNTED;
  FRESULT r = f_open(&fil, path, FA_READ);
  if (r != FR_OK) return map(r);
  sdfs_status_t st = SDFS_OK;
  FSIZE_t size = f_size(&fil);
  if (size > max) st = SDFS_TOO_BIG;
  else {
    UINT got = 0;
    r = f_read(&fil, buf, (UINT)size, &got);
    if (r != FR_OK) st = map(r);
    else if (got != size) st = SDFS_IO; /* chain shorter than the size in the directory entry */
    else *n = got;
  }
  close_fil();
  if (st != SDFS_OK) memset(buf, 0, max);
  return st;
}

#define PATH_MAX_LEN 120u

sdfs_status_t sdfs_write_file(const char *path, const uint8_t *data, size_t n) {
  if (!M.mounted) return SDFS_NOT_MOUNTED;
  size_t len = strlen(path);
  if (len == 0 || len > PATH_MAX_LEN) return SDFS_BAD_NAME;
  char tmp[PATH_MAX_LEN + 5];
  memcpy(tmp, path, len);
  memcpy(tmp + len, ".tmp", 5);

  FRESULT r = f_open(&fil, tmp, FA_WRITE | FA_CREATE_ALWAYS);
  if (r != FR_OK) return map(r);
  UINT put = 0;
  r = f_write(&fil, data, (UINT)n, &put);
  if (r == FR_OK && put != n) r = FR_DENIED; /* disk full */
  if (r == FR_OK) r = f_sync(&fil);
  FRESULT rc = f_close(&fil);
  memset(&fil, 0, sizeof(fil));
  if (r == FR_OK) r = rc;
  if (r != FR_OK) {
    f_unlink(tmp);
    return map(r);
  }
  r = f_unlink(path);
  if (r != FR_OK && r != FR_NO_FILE) return map(r);
  return map(f_rename(tmp, path));
}

sdfs_status_t sdfs_remove(const char *path) {
  if (!M.mounted) return SDFS_NOT_MOUNTED;
  return map(f_unlink(path));
}

sdfs_status_t sdfs_list(const char *dir, sdfs_dir_cb cb, void *ctx) {
  if (!M.mounted) return SDFS_NOT_MOUNTED;
  static DIR d;
  static FILINFO fi;
  FRESULT r = f_opendir(&d, dir);
  if (r != FR_OK) return map(r);
  for (;;) {
    r = f_readdir(&d, &fi);
    if (r != FR_OK || fi.fname[0] == 0) break;
    if (!cb(fi.fname, fi.fsize, (fi.fattrib & AM_DIR) != 0, ctx)) break;
  }
  f_closedir(&d);
  return map(r);
}

#if FF_USE_MKFS
/* PC test only (tools/fs_test.py builds with FF_USE_MKFS=1; the device build has no formatter).
   fmt: FM_FAT, FM_FAT32, FM_EXFAT, optionally | FM_SFD for no partition table. */
sdfs_status_t sdfs_test_format(uint8_t fmt);
sdfs_status_t sdfs_test_format(uint8_t fmt) {
  sdfs_info_t info;
  sdfs_unmount();
  if (sd_open(&info.card) != SD_OK) return SDFS_NO_CARD;
  M.card = true;
  M.blocks = info.card.blocks;
  static BYTE work[FF_MAX_SS * 8];
  MKFS_PARM opt = {.fmt = fmt, .n_fat = 0, .align = 0, .n_root = 0, .au_size = 0};
  FRESULT r = f_mkfs("", &opt, work, sizeof(work));
  sdfs_unmount();
  return map(r);
}

/* PC test only: where the mounted volume keeps its structures (for targeted corruption). */
void sdfs_test_layout(uint32_t out[4]);
void sdfs_test_layout(uint32_t out[4]) {
  out[0] = (uint32_t)fs.volbase;
  out[1] = (uint32_t)fs.fatbase;
  out[2] = (uint32_t)(fs.fs_type == FS_FAT12 || fs.fs_type == FS_FAT16 ? fs.dirbase
                                                                       : fs.database + (fs.dirbase - 2) * fs.csize);
  out[3] = (uint32_t)fs.database;
}
#endif

const char *sdfs_status_name(sdfs_status_t s) {
  switch (s) {
    case SDFS_OK: return "ok";
    case SDFS_NO_CARD: return "no card";
    case SDFS_CARD_ERROR: return "card would not start";
    case SDFS_NOT_FORMATTED: return "not FAT32/exFAT (or GPT)";
    case SDFS_IO: return "read/write error";
    case SDFS_NOT_MOUNTED: return "card not mounted";
    case SDFS_NOT_FOUND: return "file not found";
    case SDFS_TOO_BIG: return "file too big";
    case SDFS_FULL: return "card full";
    case SDFS_WRITE_PROTECTED: return "write protected";
    case SDFS_BAD_NAME: return "bad file name";
  }
  return "?";
}

const char *sdfs_type_name(sdfs_type_t t) {
  switch (t) {
    case SDFS_FAT12: return "FAT12";
    case SDFS_FAT16: return "FAT16";
    case SDFS_FAT32: return "FAT32";
    case SDFS_EXFAT: return "exFAT";
  }
  return "?";
}
