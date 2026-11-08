#ifndef APP_SDBACKUP_H
#define APP_SDBACKUP_H

/*
 * Encrypted wallet backups on the microSD card (milestone M6): the glue between the UI flows,
 * the vault, the card (app/sdfs.h) and the file format (wallet/backup.h).
 *
 * Files: "tredder-xxxxxx.trd" in the card's root folder, xxxxxx from the random salt (names say
 * nothing about the wallet). Settings: Argon2id with BK_KDF_KIB KiB and BK_KDF_PASSES passes
 * (the RAM this board can spare; the password is 128-bit random, see wallet/backup.h).
 *
 * Demo builds (no security chip) mark their backups DEMO and restore only DEMO backups; secure
 * builds refuse DEMO backups. The flag is authenticated, so it cannot be changed without the
 * password.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wallet/backup.h"

#define BK_MAX_FILES 8
#define BK_NAME_LEN 20 /* "tredder-xxxxxx.trd" + NUL */
#define BK_KDF_KIB 64u
#ifndef BK_KDF_PASSES
#define BK_KDF_PASSES 3u
#endif

typedef enum {
  BK_OK = 0,
  BK_NO_CARD,
  BK_CARD_ERROR,
  BK_NOT_FORMATTED,
  BK_IO,
  BK_FULL,
  BK_VERIFY_FAILED, /* written, but reading it back did not give the same backup */
  BK_NO_MEMORY,
  BK_ENTROPY,
  BK_LOCKED,
  BK_NO_FILES,
  BK_NOT_BACKUP,
  BK_DAMAGED,
  BK_TOO_STRONG,    /* needs more memory than this device has */
  BK_DEMO_ONLY,     /* demo build, real backup: refused */
  BK_NOT_FOR_DEMO,  /* secure build, demo backup: refused */
  BK_WRONG_PASSWORD,
  BK_PASSWORD_TYPO, /* the 12 words fail their checksum: nothing was tried */
} bk_status_t;

/* Encrypt the unlocked wallet with the 12-word password, write it, read it back and decrypt the
   copy from the card to prove it restores. name gets the file name. */
bk_status_t bk_create(const uint16_t pw[WL_BACKUP_PW_WORDS], char name[BK_NAME_LEN]);

/* Backup files in the card's root folder (sorted, at most BK_MAX_FILES). */
bk_status_t bk_list(char names[BK_MAX_FILES][BK_NAME_LEN], size_t *n);

/* Read one file and check structure, checksum and demo/secure kind (no password yet). */
bk_status_t bk_load(const char *name, uint8_t file[WL_BACKUP_FILE_LEN]);

/* Decrypt a loaded file. The caller wipes *out after use. */
bk_status_t bk_decrypt(const uint8_t file[WL_BACKUP_FILE_LEN], const uint16_t pw[WL_BACKUP_PW_WORDS],
                       wl_backup_payload_t *out);

/* Two short lines (<= 24 chars each) for the screen. */
void bk_status_text(bk_status_t s, const char **l1, const char **l2);

#endif
