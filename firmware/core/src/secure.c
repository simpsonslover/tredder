#include "wallet/secure.h"

#include "consteq.h"
#include "memzero.h"

void wl_wipe(void *p, size_t n) {
  if (p && n) memzero(p, n);
}

bool wl_ct_equal(const void *a, const void *b, size_t n) { return consteq(a, b, n); }
