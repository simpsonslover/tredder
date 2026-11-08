#ifndef APP_SDFS_H
#define APP_SDFS_H

/*
 * Files on the microSD card (milestone M6: encrypted backups). FAT12/16/32 and exFAT, one
 * partition (MBR) or none ("superfloppy"); GPT is not supported. Built on FatFs
 * (third_party/fatfs) over the block driver in app/sd.h.
 *
 * The card is untrusted removable media. Everything read from it is bounded here (files larger
 * than the caller's buffer are refused, never truncated) and must still be authenticated by the
 * caller: backups are AES-256-GCM, so a tampered file fails to decrypt instead of being used.
 * Nothing secret is ever written in plaintext.
 *
 * Usage: sdfs_mount() powers the card and mounts it; sdfs_unmount() unmounts and powers it off.
 * Only one volume, single-threaded.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app/sd.h"

typedef enum {
  SDFS_OK = 0,
  SDFS_NO_CARD,        /* nothing answered (no card, no power, wiring) */
  SDFS_CARD_ERROR,     /* card answered but would not initialise */
  SDFS_NOT_FORMATTED,  /* no FAT32/exFAT volume found (blank, NTFS, GPT, damaged boot sector) */
  SDFS_IO,             /* read/write failed or the filesystem structures are inconsistent */
  SDFS_NOT_MOUNTED,
  SDFS_NOT_FOUND,
  SDFS_TOO_BIG,        /* file larger than the caller's buffer */
  SDFS_FULL,           /* no space left (or root directory full on FAT12/16) */
  SDFS_WRITE_PROTECTED,
  SDFS_BAD_NAME,
} sdfs_status_t;

typedef enum { SDFS_FAT12 = 1, SDFS_FAT16, SDFS_FAT32, SDFS_EXFAT } sdfs_type_t;

typedef struct {
  sd_info_t card;
  sdfs_type_t type;
  uint32_t cluster_bytes;
  uint64_t volume_bytes;  /* data area size */
} sdfs_info_t;

sdfs_status_t sdfs_mount(sdfs_info_t *info);
void sdfs_unmount(void);

/* Free space. Can take a few seconds on a large FAT32 card whose free-space hint is missing. */
sdfs_status_t sdfs_free(uint64_t *bytes);

/* Whole-file read into buf. Refuses (SDFS_TOO_BIG) a file larger than max. */
sdfs_status_t sdfs_read_file(const char *path, uint8_t *buf, size_t max, size_t *n);

/* Whole-file write, replacing any existing file. Written to "<path>.tmp" first and flushed, then
   renamed over the old file, so a power cut leaves the old file or the new one (plus at worst a
   stray .tmp), never a half-written file under the real name. Callers read back and verify. */
sdfs_status_t sdfs_write_file(const char *path, const uint8_t *data, size_t n);

sdfs_status_t sdfs_remove(const char *path);

/* Lists a directory ("" or "/" = root). The callback returns false to stop early. */
typedef bool (*sdfs_dir_cb)(const char *name, uint64_t size, bool is_dir, void *ctx);
sdfs_status_t sdfs_list(const char *dir, sdfs_dir_cb cb, void *ctx);

const char *sdfs_status_name(sdfs_status_t s);
const char *sdfs_type_name(sdfs_type_t t);

#endif
