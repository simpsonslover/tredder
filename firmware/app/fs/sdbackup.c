/* Encrypted wallet backups on the microSD card. See app/sdbackup.h. */
#include "app/sdbackup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app/hal.h"
#include "app/rng.h"
#include "app/sdfs.h"
#include "app/vault.h"
#include "wallet/secure.h"

#ifdef TREDDER_DEMO_NO_SE
#define IS_DEMO true
#else
#define IS_DEMO false
#endif

#define WORK_BYTES ((size_t)BK_KDF_KIB * 1024u)

static bk_status_t from_sdfs(sdfs_status_t s) {
  switch (s) {
    case SDFS_OK: return BK_OK;
    case SDFS_NO_CARD: return BK_NO_CARD;
    case SDFS_CARD_ERROR: return BK_CARD_ERROR;
    case SDFS_NOT_FORMATTED: return BK_NOT_FORMATTED;
    case SDFS_FULL: return BK_FULL;
    case SDFS_NOT_FOUND: return BK_NO_FILES;
    case SDFS_TOO_BIG: return BK_NOT_BACKUP;
    default: return BK_IO;
  }
}

static bk_status_t mount(void) {
  sdfs_info_t info;
  sdfs_status_t s = sdfs_mount(&info);
  if (s == SDFS_OK) hal_log("microSD: %s, %lu MB", sdfs_type_name(info.type), (unsigned long)(info.volume_bytes >> 20));
  else hal_log("microSD: %s", sdfs_status_name(s));
  return from_sdfs(s);
}

/* The Argon2 work area: only during a backup or restore, from the heap (64 KiB of the RAM the
   firmware leaves free), wiped by wallet/backup.c before it is returned. */
static void *work_alloc(void) { return malloc(WORK_BYTES); }
static void work_free(void *w) {
  if (!w) return;
  wl_wipe(w, WORK_BYTES);
  free(w);
}

bk_status_t bk_create(const uint16_t pw[WL_BACKUP_PW_WORDS], char name[BK_NAME_LEN]) {
  name[0] = 0;
  wl_backup_payload_t p, back;
  memset(&p, 0, sizeof(p));
  if (vault_export_backup(p.idx, &p.words, &p.blind_signing) != VAULT_OK) return BK_LOCKED;

  uint8_t rnd[WL_BACKUP_SALT_LEN + WL_BACKUP_NONCE_LEN];
  static uint8_t file[WL_BACKUP_FILE_LEN], again[WL_BACKUP_FILE_LEN];
  bk_status_t st = BK_OK;
  void *work = NULL;
  /* Salt and nonce: fresh from TRNG + SE through the health-tested pool (never reused). */
  if (!rng_collect(rnd, sizeof(rnd))) st = BK_ENTROPY;
  else if (!(work = work_alloc())) st = BK_NO_MEMORY;
  if (st == BK_OK) {
    wl_kdf_t kdf = {BK_KDF_KIB, BK_KDF_PASSES};
    uint32_t t0 = hal_millis();
    wl_status_t w = wl_backup_seal(&p, pw, IS_DEMO, kdf, rnd, rnd + WL_BACKUP_SALT_LEN, work, WORK_BYTES, file);
    hal_log("backup: encrypted in %lu ms", (unsigned long)(hal_millis() - t0));
    if (w != WL_OK) st = w == WL_ERR_CHECKSUM ? BK_PASSWORD_TYPO : BK_IO;
  }
  if (st == BK_OK) st = mount();
  if (st == BK_OK) {
    snprintf(name, BK_NAME_LEN, "tredder-%02x%02x%02x.trd", rnd[0], rnd[1], rnd[2]);
    st = from_sdfs(sdfs_write_file(name, file, sizeof(file)));
    if (st == BK_OK) { /* read back the copy on the card and prove it restores to the same wallet */
      size_t n = 0;
      sdfs_status_t r = sdfs_read_file(name, again, sizeof(again), &n);
      if (r != SDFS_OK || n != sizeof(again) || memcmp(again, file, sizeof(file)) != 0) st = BK_VERIFY_FAILED;
      else if (wl_backup_open(again, n, pw, work, WORK_BYTES, &back) != WL_OK ||
               back.words != p.words || back.blind_signing != p.blind_signing ||
               !wl_ct_equal(back.idx, p.idx, sizeof(p.idx)))
        st = BK_VERIFY_FAILED;
    }
    if (st == BK_VERIFY_FAILED) sdfs_remove(name); /* never leave a backup that does not restore */
    sdfs_unmount();
  }
  work_free(work);
  wl_wipe(&p, sizeof(p));
  wl_wipe(&back, sizeof(back));
  wl_wipe(rnd, sizeof(rnd));
  if (st != BK_OK) name[0] = 0;
  hal_log("backup: %s%s", st == BK_OK ? "saved as " : "failed, code ", st == BK_OK ? name : "");
  if (st != BK_OK) hal_log("backup: code %d", (int)st);
  return st;
}

typedef struct {
  char (*names)[BK_NAME_LEN];
  size_t n;
} list_t;

static bool is_backup_name(const char *s) {
  size_t n = strlen(s);
  return n == 18 && !strncmp(s, "tredder-", 8) && !strcmp(s + 14, ".trd");
}

static bool list_cb(const char *name, uint64_t size, bool is_dir, void *ctx) {
  list_t *l = ctx;
  if (!is_dir && size == WL_BACKUP_FILE_LEN && is_backup_name(name) && l->n < BK_MAX_FILES)
    snprintf(l->names[l->n++], BK_NAME_LEN, "%s", name);
  return true;
}

bk_status_t bk_list(char names[BK_MAX_FILES][BK_NAME_LEN], size_t *n) {
  *n = 0;
  bk_status_t st = mount();
  if (st != BK_OK) return st;
  list_t l = {names, 0};
  st = from_sdfs(sdfs_list("", list_cb, &l));
  sdfs_unmount();
  for (size_t i = 1; i < l.n; i++) /* sort: same order every time */
    for (size_t k = i; k > 0 && strcmp(names[k - 1], names[k]) > 0; k--) {
      char t[BK_NAME_LEN];
      memcpy(t, names[k], BK_NAME_LEN), memcpy(names[k], names[k - 1], BK_NAME_LEN), memcpy(names[k - 1], t, BK_NAME_LEN);
    }
  *n = l.n;
  if (st == BK_OK && l.n == 0) st = BK_NO_FILES;
  return st;
}

bk_status_t bk_load(const char *name, uint8_t file[WL_BACKUP_FILE_LEN]) {
  memset(file, 0, WL_BACKUP_FILE_LEN);
  bk_status_t st = mount();
  if (st != BK_OK) return st;
  size_t n = 0;
  st = from_sdfs(sdfs_read_file(name, file, WL_BACKUP_FILE_LEN, &n));
  sdfs_unmount();
  if (st != BK_OK) return st;
  wl_backup_info_t info;
  wl_status_t w = wl_backup_inspect(file, n, &info);
  if (w == WL_ERR_DAMAGED) return BK_DAMAGED;
  if (w != WL_OK) return BK_NOT_BACKUP;
  if (info.demo != IS_DEMO) return IS_DEMO ? BK_DEMO_ONLY : BK_NOT_FOR_DEMO;
  if ((uint64_t)info.kdf.m_kib * 1024u > WORK_BYTES) return BK_TOO_STRONG;
  return BK_OK;
}

bk_status_t bk_decrypt(const uint8_t file[WL_BACKUP_FILE_LEN], const uint16_t pw[WL_BACKUP_PW_WORDS],
                       wl_backup_payload_t *out) {
  memset(out, 0, sizeof(*out));
  void *work = work_alloc();
  if (!work) return BK_NO_MEMORY;
  uint32_t t0 = hal_millis();
  wl_status_t w = wl_backup_open(file, WL_BACKUP_FILE_LEN, pw, work, WORK_BYTES, out);
  hal_log("restore: decrypt took %lu ms", (unsigned long)(hal_millis() - t0));
  work_free(work);
  switch (w) {
    case WL_OK: break;
    case WL_ERR_CHECKSUM: return BK_PASSWORD_TYPO;
    case WL_ERR_AUTH: return BK_WRONG_PASSWORD;
    case WL_ERR_DAMAGED: return BK_DAMAGED;
    case WL_ERR_ARG: return BK_TOO_STRONG;
    default: return BK_NOT_BACKUP;
  }
  /* the demo/secure rule again, now on the authenticated flag */
  wl_backup_info_t info;
  if (wl_backup_inspect(file, WL_BACKUP_FILE_LEN, &info) != WL_OK || info.demo != IS_DEMO) {
    wl_wipe(out, sizeof(*out));
    return IS_DEMO ? BK_DEMO_ONLY : BK_NOT_FOR_DEMO;
  }
  return BK_OK;
}

void bk_status_text(bk_status_t s, const char **l1, const char **l2) {
  const char *a = "", *b = "";
  switch (s) {
    case BK_OK: a = "Done."; break;
    case BK_NO_CARD: a = "No microSD card found.", b = "Insert one and retry."; break;
    case BK_CARD_ERROR: a = "The card did not start.", b = "Re-insert it, retry."; break;
    case BK_NOT_FORMATTED: a = "Card not FAT32/exFAT.", b = "Format it (MBR), retry."; break;
    case BK_IO: a = "Card read/write error.", b = "Try again or another card."; break;
    case BK_FULL: a = "The card is full."; break;
    case BK_VERIFY_FAILED: a = "Check after writing", b = "failed. Nothing kept."; break;
    case BK_NO_MEMORY: a = "Out of memory.", b = "Restart and retry."; break;
    case BK_ENTROPY: a = "Random source failed", b = "its health test."; break;
    case BK_LOCKED: a = "Unlock the wallet first."; break;
    case BK_NO_FILES: a = "No tredder backup", b = "on this card."; break;
    case BK_NOT_BACKUP: a = "Not a tredder backup", b = "(or a newer version)."; break;
    case BK_DAMAGED: a = "The backup file is", b = "damaged (checksum)."; break;
    case BK_TOO_STRONG: a = "Backup needs more", b = "memory than this device."; break;
    case BK_DEMO_ONLY: a = "Real-device backup:", b = "a demo never loads it."; break;
    case BK_NOT_FOR_DEMO: a = "Demo backup (test seed):", b = "not for a real device."; break;
    case BK_WRONG_PASSWORD: a = "Wrong backup password", b = "(or file was altered)."; break;
    case BK_PASSWORD_TYPO: a = "A password word is", b = "wrong (checksum)."; break;
  }
  *l1 = a, *l2 = b;
}
