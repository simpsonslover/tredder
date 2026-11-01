#ifndef WALLET_SECURE_H
#define WALLET_SECURE_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Memory hygiene for secrets (seed, keys, mnemonic words, PIN).
 *
 * Rules used throughout the firmware:
 *  - Secrets live in caller-owned buffers on the stack or in dedicated static
 *    buffers, never in heap memory.
 *  - Every function that touches a secret wipes its own temporaries before
 *    returning, on success AND on every error path.
 *  - Comparisons involving secrets are constant time.
 */

/* Zero memory in a way the compiler cannot optimise away (trezor-crypto memzero). */
void wl_wipe(void *p, size_t n);

/* Constant-time equality (time depends only on n). */
bool wl_ct_equal(const void *a, const void *b, size_t n);

#endif
