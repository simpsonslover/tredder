#ifndef WALLET_STATUS_H
#define WALLET_STATUS_H

/* Result codes shared by every wallet-core function. 0 is success. */
typedef enum {
  WL_OK = 0,
  WL_ERR_ARG,             /* invalid argument / unsupported combination */
  WL_ERR_BUFFER,          /* output buffer too small */
  WL_ERR_ENTROPY_LOW,     /* not enough credited entropy from every required source */
  WL_ERR_ENTROPY_HEALTH,  /* TRNG failed a continuous health test: stop, do not retry silently */
  WL_ERR_WORD_COUNT,      /* mnemonic is not 12/15/18/21/24 words */
  WL_ERR_UNKNOWN_WORD,    /* a word is not in the BIP39 English list */
  WL_ERR_CHECKSUM,        /* all words valid but the BIP39 checksum does not match */
  WL_ERR_PASSPHRASE,      /* passphrase has characters the device cannot enter (see mnemonic.h) */
  WL_ERR_CRYPTO,          /* underlying library reported failure (invalid key, etc.) */
  WL_ERR_FORMAT,          /* not a file of this kind, or a version/setting this firmware does not accept */
  WL_ERR_DAMAGED,         /* file structure fine but its checksum does not match (corrupted) */
  WL_ERR_AUTH,            /* decryption failed: wrong password, or the file was altered */
} wl_status_t;

#endif
