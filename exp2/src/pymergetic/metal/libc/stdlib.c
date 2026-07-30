/*
 * metal/libc stdlib — firmware-host only (-nostdinc consumers).
 * Guests must not link this; use Metal mem/log/console APIs instead.
 */
#include <stdlib.h>

int atoi(const char *s)
{
  int neg = 0;
  int v = 0;

  /* s is nonnull (C / clang builtin); NULL is UB. */
  while (*s == ' ' || *s == '\t') {
    s++;
  }
  if (*s == '+' || *s == '-') {
    neg = (*s == '-');
    s++;
  }
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    s++;
  }
  return neg ? -v : v;
}
