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

char *strcpy(char *dst, const char *src)
{
  size_t i;

  for (i = 0; src[i] != '\0'; i++) {
    dst[i] = src[i];
  }
  dst[i] = '\0';
  return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
  size_t i;

  for (i = 0; i < n && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }
  for (; i < n; i++) {
    dst[i] = '\0';
  }
  return dst;
}

char *strstr(const char *haystack, const char *needle)
{
  size_t nlen;
  size_t i;

  if (haystack == NULL || needle == NULL) {
    return (char *)0;
  }
  if (needle[0] == '\0') {
    return (char *)haystack;
  }
  nlen = strlen(needle);
  for (i = 0; haystack[i] != '\0'; i++) {
    size_t j;
    for (j = 0; j < nlen; j++) {
      if (haystack[i + j] != needle[j]) {
        break;
      }
    }
    if (j == nlen) {
      return (char *)(haystack + i);
    }
  }
  return (char *)0;
}

char *strchr(const char *s, int c)
{
  char ch = (char)c;

  for (;;) {
    if (*s == ch) {
      return (char *)s;
    }
    if (*s == '\0') {
      return (char *)0;
    }
    s++;
  }
}

/* strrchr: freestanding libc helper (UEFI/bios link). */

size_t strspn(const char *s, const char *accept)
{
  size_t n = 0;
  size_t i;

  for (; s[n] != '\0'; n++) {
    for (i = 0; accept[i] != '\0'; i++) {
      if (s[n] == accept[i]) {
        break;
      }
    }
    if (accept[i] == '\0') {
      break;
    }
  }
  return n;
}

size_t strcspn(const char *s, const char *reject)
{
  size_t n = 0;
  size_t i;

  for (; s[n] != '\0'; n++) {
    for (i = 0; reject[i] != '\0'; i++) {
      if (s[n] == reject[i]) {
        return n;
      }
    }
  }
  return n;
}

char *strtok_r(char *str, const char *delim, char **saveptr)
{
  char *start;
  char *end;

  if (str != (char *)0) {
    *saveptr = str;
  }
  start = *saveptr;
  if (start == (char *)0 || *start == '\0') {
    return (char *)0;
  }
  start += strspn(start, delim);
  if (*start == '\0') {
    *saveptr = start;
    return (char *)0;
  }
  end = start + strcspn(start, delim);
  if (*end != '\0') {
    *end = '\0';
    *saveptr = end + 1;
  } else {
    *saveptr = end;
  }
  return start;
}

char *strtok_s(char *str, const char *delim, char **ctx)
{
  return strtok_r(str, delim, ctx);
}
