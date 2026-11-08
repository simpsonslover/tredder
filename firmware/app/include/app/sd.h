#ifndef APP_SD_H
#define APP_SD_H

/*
 * microSD card, 512-byte blocks (firmware milestone M6: encrypted backups).
 *   platform/rp2350/sd_spi.c  SPI mode on SPI0 (GP0-3), CRC checked on every command and block
 *   platform/emu              (later) a card image file
 * The card is untrusted removable media: everything read from it is validated by the caller,
 * and it is only powered while a backup flow uses it (Q3 on the PCB).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SD_BLOCK 512u

/* SPI data clock after initialisation (initialisation itself runs at 400 kHz). */
#ifndef SD_FAST_HZ
#define SD_FAST_HZ (4u * 1000u * 1000u)
#endif

typedef enum {
  SD_OK = 0,
  SD_NO_CARD,   /* nothing answered CMD0 (no card, no power, or bad wiring) */
  SD_UNUSABLE,  /* card answered but refused to initialise (voltage, version, timeout) */
  SD_IO,        /* a command or block failed: CRC error, timeout, card error */
  SD_RANGE,     /* block number beyond the card */
} sd_status_t;

typedef struct {
  bool sdhc;             /* block addressed (SDHC/SDXC) vs byte addressed (SDSC) */
  uint32_t blocks;       /* capacity in 512-byte blocks */
  uint8_t cid[16];       /* card identification register */
  uint8_t mid;           /* manufacturer id (from CID) */
  char oem[3], name[6];  /* from CID, NUL-terminated */
  uint32_t serial;       /* from CID */
} sd_info_t;

/* Power the card and initialise it. Fills info on success. */
sd_status_t sd_open(sd_info_t *info);
/* Power the card off (I/O pins low first). Safe to call when closed. */
void sd_close(void);
sd_status_t sd_read(uint32_t block, uint8_t out[SD_BLOCK]);
sd_status_t sd_write(uint32_t block, const uint8_t data[SD_BLOCK]);
/* Data clock after initialisation (default SD_FAST_HZ). For bring-up speed tests. */
void sd_set_clock(uint32_t hz);
const char *sd_status_name(sd_status_t s);

#endif
