/*
 * PC tests for app/fs (sdfs + FatFs) on a RAM card. Run by tools/fs_test.py.
 *   1. every supported layout: FAT16, FAT32 and exFAT, with an MBR and without ("superfloppy")
 *   2. file operations: write / overwrite / read back / long names / list / free / remove / remount
 *   3. errors: no card, blank card, buffer too small, missing file
 *   4. hostile cards: random corruption of the filesystem metadata, then mount + read + write.
 *      Must never crash, hang, or touch a block outside the card. (No AddressSanitizer on this
 *      toolchain: silent out-of-bounds accesses inside FatFs would not be caught here.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app/sdfs.h"
#include "ff.h"
#include "ram_sd.h"

sdfs_status_t sdfs_test_format(uint8_t fmt);
void sdfs_test_layout(uint32_t out[4]);

static int fails, checks;
#define CHECK(c)                                                     \
  do {                                                               \
    checks++;                                                        \
    if (!(c)) {                                                      \
      fails++;                                                       \
      printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);          \
    }                                                                \
  } while (0)
#define CHECK_ST(expr, want)                                                            \
  do {                                                                                  \
    sdfs_status_t _s = (expr);                                                          \
    checks++;                                                                           \
    if (_s != (want)) {                                                                 \
      fails++;                                                                          \
      printf("  FAIL %s:%d: %s -> %s, want %s\n", __FILE__, __LINE__, #expr,            \
             sdfs_status_name(_s), sdfs_status_name(want));                             \
    }                                                                                   \
  } while (0)

static uint32_t rng = 0x12345678u;
static uint32_t rnd(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

static void fill(uint8_t *b, size_t n, uint32_t seed) {
  for (size_t i = 0; i < n; i++) b[i] = (uint8_t)(seed * 2654435761u + i * 40503u >> 7);
}

typedef struct {
  int files, dirs;
  bool saw_long;
} listing_t;

static bool count_cb(const char *name, uint64_t size, bool is_dir, void *ctx) {
  listing_t *l = ctx;
  (void)size;
  if (is_dir) l->dirs++;
  else l->files++;
  if (!strcmp(name, "tredder-backup-2026-11-08.bin")) l->saw_long = true;
  return true;
}

static uint8_t a[70000], b[70000], rd[70000];

/* ------------------------------------------------------------ 1+2: layouts and file operations */
static void layout(const char *label, uint32_t blocks, uint8_t fmt, sdfs_type_t want) {
  printf("%s, %lu MB\n", label, (unsigned long)(blocks / 2048));
  ram_sd_create(blocks);
  CHECK_ST(sdfs_test_format(fmt), SDFS_OK);
  bool sfd = (fmt & FM_SFD) != 0;
  CHECK(ram_img[510] == 0x55 && ram_img[511] == 0xAA);
  if (!sfd) CHECK(ram_img[446 + 4] != 0); /* MBR partition entry present */

  sdfs_info_t info;
  CHECK_ST(sdfs_mount(&info), SDFS_OK);
  CHECK(info.type == want);
  CHECK(info.volume_bytes > 0 && info.volume_bytes <= (uint64_t)blocks * SD_BLOCK);
  uint64_t free0 = 0, free1 = 0;
  CHECK_ST(sdfs_free(&free0), SDFS_OK);

  fill(a, sizeof(a), 1);
  fill(b, 1234, 2);
  size_t n = 0;
  CHECK_ST(sdfs_write_file("BACKUP.TRD", a, 3000), SDFS_OK);
  CHECK_ST(sdfs_read_file("BACKUP.TRD", rd, sizeof(rd), &n), SDFS_OK);
  CHECK(n == 3000 && !memcmp(rd, a, 3000));
  /* overwrite with a different size: the .tmp + rename path */
  CHECK_ST(sdfs_write_file("BACKUP.TRD", b, 1234), SDFS_OK);
  CHECK_ST(sdfs_read_file("BACKUP.TRD", rd, sizeof(rd), &n), SDFS_OK);
  CHECK(n == 1234 && !memcmp(rd, b, 1234));
  CHECK_ST(sdfs_read_file("BACKUP.TRD.tmp", rd, sizeof(rd), &n), SDFS_NOT_FOUND);
  /* long name, multi-cluster file */
  CHECK_ST(sdfs_write_file("tredder-backup-2026-11-08.bin", a, sizeof(a)), SDFS_OK);
  CHECK_ST(sdfs_read_file("tredder-backup-2026-11-08.bin", rd, sizeof(rd), &n), SDFS_OK);
  CHECK(n == sizeof(a) && !memcmp(rd, a, sizeof(a)));
  /* empty file */
  CHECK_ST(sdfs_write_file("EMPTY", a, 0), SDFS_OK);
  CHECK_ST(sdfs_read_file("EMPTY", rd, sizeof(rd), &n), SDFS_OK);
  CHECK(n == 0);
  /* errors */
  CHECK_ST(sdfs_read_file("BACKUP.TRD", rd, 100, &n), SDFS_TOO_BIG);
  CHECK(n == 0);
  CHECK_ST(sdfs_read_file("NOPE.BIN", rd, sizeof(rd), &n), SDFS_NOT_FOUND);
  CHECK_ST(sdfs_write_file("bad*name", a, 10), SDFS_BAD_NAME);
  CHECK_ST(sdfs_free(&free1), SDFS_OK);
  CHECK(free1 < free0);

  listing_t l = {0};
  CHECK_ST(sdfs_list("", count_cb, &l), SDFS_OK);
  CHECK(l.files == 3 && l.dirs == 0 && l.saw_long);

  /* remount: everything is really on the card */
  sdfs_unmount();
  CHECK(!ram_open); /* card powered off */
  CHECK_ST(sdfs_read_file("BACKUP.TRD", rd, sizeof(rd), &n), SDFS_NOT_MOUNTED);
  CHECK_ST(sdfs_mount(&info), SDFS_OK);
  CHECK_ST(sdfs_read_file("tredder-backup-2026-11-08.bin", rd, sizeof(rd), &n), SDFS_OK);
  CHECK(n == sizeof(a) && !memcmp(rd, a, sizeof(a)));
  CHECK_ST(sdfs_remove("EMPTY"), SDFS_OK);
  CHECK_ST(sdfs_read_file("EMPTY", rd, sizeof(rd), &n), SDFS_NOT_FOUND);
  /* write-protected card (writes fail at the block level) */
  ram_readonly = true;
  CHECK(sdfs_write_file("NEW.BIN", a, 5000) != SDFS_OK);
  ram_readonly = false;
  sdfs_unmount();
}

/* ------------------------------------------------------------ 3: no card, blank card */
static void errors(void) {
  printf("errors: no card, blank card\n");
  sdfs_info_t info;
  ram_sd_create(16384);
  ram_present = false;
  CHECK_ST(sdfs_mount(&info), SDFS_NO_CARD);
  ram_present = true;
  CHECK_ST(sdfs_mount(&info), SDFS_NOT_FORMATTED); /* all zeros */
  CHECK(!ram_open);                                 /* failed mount powers the card off */
  /* protective MBR of a GPT disk */
  ram_img[446 + 4] = 0xEE;
  ram_img[446 + 8] = 1;
  ram_img[510] = 0x55, ram_img[511] = 0xAA;
  CHECK_ST(sdfs_mount(&info), SDFS_NOT_FORMATTED);
}

/* ------------------------------------------------------------ 4: hostile cards */
static void fuzz(const char *label, uint32_t blocks, uint8_t fmt, int iters) {
  printf("corrupted %s, %d cards\n", label, iters);
  ram_sd_create(blocks);
  CHECK_ST(sdfs_test_format(fmt), SDFS_OK);
  sdfs_info_t info;
  CHECK_ST(sdfs_mount(&info), SDFS_OK);
  fill(a, sizeof(a), 3);
  CHECK_ST(sdfs_write_file("BACKUP.TRD", a, 20000), SDFS_OK);
  CHECK_ST(sdfs_write_file("tredder-backup-2026-11-08.bin", a, 9000), SDFS_OK);
  uint32_t lay[4]; /* volume start, FAT start, root directory, data area */
  sdfs_test_layout(lay);
  sdfs_unmount();
  /* Corruption targets, in blocks: MBR, boot region (exFAT: 12 sectors + backup), start of the
     FAT, the root directory, and the start of the data area (exFAT: allocation bitmap, up-case
     table). Plus anywhere in the first 600 KB. */
  const uint32_t tgt[][2] = {{0, 1}, {lay[0], 24}, {lay[1], 8}, {lay[2], 4}, {lay[3], 32}, {0, 1200}};

  size_t bytes = (size_t)blocks * SD_BLOCK;
  uint8_t *clean = malloc(bytes);
  memcpy(clean, ram_img, bytes);
  int mounted = 0, read_ok = 0;
  double worst = 0;
  unsigned long most_reads = 0;
  for (int it = 0; it < iters; it++) {
    memcpy(ram_img, clean, bytes);
    int flips = 1 + (int)(rnd() % 24);
    for (int k = 0; k < flips; k++) {
      const uint32_t *t = tgt[rnd() % 6];
      size_t at = ((size_t)t[0] + rnd() % t[1]) * SD_BLOCK + rnd() % SD_BLOCK;
      if (at >= bytes) continue;
      switch (rnd() % 3) {
        case 0: ram_img[at] ^= (uint8_t)(1u << (rnd() % 8)); break;
        case 1: ram_img[at] = (uint8_t)rnd(); break;
        default: ram_img[at] = (rnd() & 1) ? 0xFF : 0x00; break;
      }
    }
    clock_t t0 = clock();
    unsigned long r0 = ram_reads + ram_writes;
    size_t n;
    listing_t l = {0};
    uint64_t fr;
    if (sdfs_mount(&info) == SDFS_OK) {
      mounted++;
      sdfs_list("", count_cb, &l);
      if (sdfs_read_file("BACKUP.TRD", rd, sizeof(rd), &n) == SDFS_OK) {
        read_ok++;
        CHECK(n <= sizeof(rd));
      }
      sdfs_read_file("tredder-backup-2026-11-08.bin", rd, sizeof(rd), &n);
      sdfs_free(&fr);
      sdfs_write_file("NEW.BIN", a, 7000);
      sdfs_write_file("BACKUP.TRD", a, 3000);
      sdfs_unmount();
    }
    CHECK(!ram_open);
    double s = (double)(clock() - t0) / CLOCKS_PER_SEC;
    if (s > worst) worst = s;
    if (ram_reads + ram_writes - r0 > most_reads) most_reads = ram_reads + ram_writes - r0;
  }
  printf("  %d mounted, %d read the backup file; slowest card %.3f s, at most %lu block I/Os\n", mounted,
         read_ok, worst, most_reads);
  CHECK(worst < 2.0); /* a pathological card must not hang the device */
  free(clean);
}

int main(int argc, char **argv) {
  int iters = argc > 1 ? atoi(argv[1]) : 3000;
  layout("FAT32 + MBR", 131072, FM_FAT32, SDFS_FAT32);
  layout("FAT32, no partition table", 131072, FM_FAT32 | FM_SFD, SDFS_FAT32);
  layout("exFAT + MBR", 131072, FM_EXFAT, SDFS_EXFAT);
  layout("exFAT, no partition table", 131072, FM_EXFAT | FM_SFD, SDFS_EXFAT);
  layout("exFAT small", 16384, FM_EXFAT, SDFS_EXFAT);
  layout("FAT16 + MBR", 32768, FM_FAT, SDFS_FAT16);
  errors();
  fuzz("FAT32", 131072, FM_FAT32, iters);
  fuzz("exFAT", 16384, FM_EXFAT, iters);
  printf("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
