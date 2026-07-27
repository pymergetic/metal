/*
 * Compile Openwall crypt_blowfish without editing external/.
 * Skip GNU crypt() conflict; use crypt_rn() from wrapper.
 * Force the portable C Blowfish path even on i386 (no x86.S).
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define __SKIP_GNU 1
#if defined(__i386__)
#undef __i386__
#endif

/* Freestanding Metal hosts may lack libc strdup(); keep a private copy
 * so Dropbear/stub string.h declaring strdup cannot conflict. */
static char *metal_bf_strdup(const char *s)
{
  size_t n;
  char  *p;

  if (s == NULL) {
    return NULL;
  }
  n = strlen(s) + 1u;
  p = (char *)malloc(n);
  if (p == NULL) {
    return NULL;
  }
  memcpy(p, s, n);
  return p;
}
#define strdup metal_bf_strdup

#include "../../../../external/crypt_blowfish/crypt_blowfish.c"
#include "../../../../external/crypt_blowfish/crypt_gensalt.c"
#include "../../../../external/crypt_blowfish/wrapper.c"
