/*
 * metal/libc stdlib — firmware-host only (-nostdinc consumers).
 * Guests must not link this; use Metal mem/log/console APIs instead.
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Linked from pymergetic_metal_rt — avoid pulling faces into libc headers. */
_Noreturn void pm_metal_rt_halt(void);

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

int abs(int j)
{
  return j < 0 ? -j : j;
}

long labs(long j)
{
  return j < 0 ? -j : j;
}

_Noreturn void abort(void)
{
  pm_metal_rt_halt();
}

/* Insertion sort — enough for WAMR native-symbol tables. */
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))
{
  uint8_t *b;
  uint8_t tmp[256];
  size_t i;
  size_t j;

  if (base == NULL || compar == NULL || size == 0 || size > sizeof(tmp)) {
    return;
  }

  b = (uint8_t *)base;
  for (i = 1; i < nmemb; i++) {
    memcpy(tmp, b + i * size, size);
    j = i;
    while (j > 0 && compar(tmp, b + (j - 1) * size) < 0) {
      memcpy(b + j * size, b + (j - 1) * size, size);
      j--;
    }
    memcpy(b + j * size, tmp, size);
  }
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *))
{
  size_t lo;
  size_t hi;

  if (key == NULL || base == NULL || compar == NULL || size == 0) {
    return NULL;
  }

  lo = 0;
  hi = nmemb;
  while (lo < hi) {
    size_t mid;
    const uint8_t *p;
    int c;

    mid = lo + (hi - lo) / 2;
    p = (const uint8_t *)base + mid * size;
    c = compar(key, p);
    if (c == 0) {
      return (void *)(uintptr_t)p;
    }
    if (c < 0) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return NULL;
}
