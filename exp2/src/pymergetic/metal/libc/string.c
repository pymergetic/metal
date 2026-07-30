/*
 * metal/libc string — firmware-host only (-nostdinc consumers).
 * Guests must not link this; use Metal mem/log/console APIs instead.
 */
#include <stddef.h>
#include <string.h>

void *memcpy(void *dst, const void *src, size_t n)
{
  unsigned char       *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  size_t               i;

  for (i = 0; i < n; i++) {
    d[i] = s[i];
  }
  return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
  unsigned char       *d = (unsigned char *)dst;
  const unsigned char *s = (const unsigned char *)src;
  size_t               i;

  if (d == s || n == 0) {
    return dst;
  }
  if (d < s) {
    for (i = 0; i < n; i++) {
      d[i] = s[i];
    }
  } else {
    for (i = n; i > 0; i--) {
      d[i - 1] = s[i - 1];
    }
  }
  return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
  const unsigned char *p = (const unsigned char *)a;
  const unsigned char *q = (const unsigned char *)b;
  size_t               i;

  for (i = 0; i < n; i++) {
    if (p[i] != q[i]) {
      return (int)p[i] - (int)q[i];
    }
  }
  return 0;
}

void *memset(void *dst, int c, size_t n)
{
  unsigned char *d = (unsigned char *)dst;
  size_t         i;

  for (i = 0; i < n; i++) {
    d[i] = (unsigned char)c;
  }
  return dst;
}

int strcmp(const char *a, const char *b)
{
  size_t i;

  /* Pointers are nonnull (C / clang builtin); NULL is UB. */
  for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
    if (a[i] != b[i]) {
      return (unsigned char)a[i] - (unsigned char)b[i];
    }
  }
  return (unsigned char)a[i] - (unsigned char)b[i];
}

int strncmp(const char *a, const char *b, size_t n)
{
  size_t i;

  if (n == 0) {
    return 0;
  }
  for (i = 0; i < n && a[i] != '\0' && b[i] != '\0'; i++) {
    if (a[i] != b[i]) {
      return (unsigned char)a[i] - (unsigned char)b[i];
    }
  }
  if (i >= n) {
    return 0;
  }
  return (unsigned char)a[i] - (unsigned char)b[i];
}

size_t strlen(const char *s)
{
  size_t n;

  for (n = 0; s[n] != '\0'; n++) {
  }
  return n;
}
