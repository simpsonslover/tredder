#ifndef APP_RNG_H
#define APP_RNG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Fresh entropy straight from TRNG + SE through the health-tested pool (<= 64 bytes). */
bool rng_collect(uint8_t *out, size_t n);

/* Unrecoverable hardware/entropy fault: show it and halt (provided by firmware.c). */
_Noreturn void rng_fatal(const char *why);

#endif
