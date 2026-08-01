/*
 * Minimal CRT for Dropbear freestanding link (malloc/stdio/assert).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>

#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/log/__init__.h>
#include <pymergetic/metal/mem/__init__.h>

#include "dropbear_stubs/stdio.h"
#include "dropbear_stubs/time.h"

void *malloc(size_t n)
{
  if (n == 0u) {
    n = 1u;
  }
  return (void *)pm_metal_mem_alloc(n);
}

void *calloc(size_t nmemb, size_t size)
{
  size_t total;
  void *p;

  if (nmemb != 0u && size > (SIZE_MAX / nmemb)) {
    return NULL;
  }
  total = nmemb * size;
  p = malloc(total);
  if (p != NULL) {
    memset(p, 0, total);
  }
  return p;
}

void *realloc(void *p, size_t n)
{
  return (void *)pm_metal_mem_realloc((uint8_t *)p, n);
}

void free(void *p)
{
  pm_metal_mem_free((uint8_t *)p);
}

char *strrchr(const char *s, int c)
{
  const char *last;
  char ch;

  if (s == NULL) {
    return NULL;
  }
  ch = (char)c;
  last = NULL;
  for (;;) {
    if (*s == ch) {
      last = s;
    }
    if (*s == '\0') {
      break;
    }
    s++;
  }
  return (char *)(uintptr_t)last;
}

static FILE g_stdin;
static FILE g_stdout;
static FILE g_stderr;
FILE *stdin = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

int fprintf(FILE *f, const char *fmt, ...)
{
  char buf[256];
  va_list ap;
  int n;

  (void)f;
  if (fmt == NULL) {
    return -1;
  }
  va_start(ap, fmt);
  n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n > 0) {
    pm_metal_log((const uint8_t *)buf);
  }
  return n;
}

FILE *fopen(const char *path, const char *mode)
{
  (void)path;
  (void)mode;
  return NULL;
}

int fclose(FILE *f)
{
  (void)f;
  return 0;
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *f)
{
  (void)ptr;
  (void)size;
  (void)nmemb;
  (void)f;
  return 0;
}

int fflush(FILE *f)
{
  (void)f;
  return 0;
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
  unsigned long v;

  (void)base;
  if (nptr == NULL) {
    if (endptr != NULL) {
      *endptr = NULL;
    }
    return 0;
  }
  while (*nptr == ' ' || *nptr == '\t') {
    nptr++;
  }
  if (*nptr == '+') {
    nptr++;
  }
  v = 0;
  while (*nptr >= '0' && *nptr <= '9') {
    v = v * 10ul + (unsigned long)(*nptr - '0');
    nptr++;
  }
  if (endptr != NULL) {
    *endptr = (char *)(uintptr_t)nptr;
  }
  return v;
}

int clock_gettime(clockid_t clk_id, struct timespec *tp)
{
  uint64_t us;

  (void)clk_id;
  if (tp == NULL) {
    return -1;
  }
  us = pm_metal_async_mono_us();
  tp->tv_sec = (time_t)(us / 1000000ull);
  tp->tv_nsec = (long)((us % 1000000ull) * 1000ull);
  return 0;
}

uint16_t htons(uint16_t x)
{
  return (uint16_t)((x << 8) | (x >> 8));
}

uint32_t htonl(uint32_t x)
{
  return ((x & 0xffu) << 24) | ((x & 0xff00u) << 8) | ((x >> 8) & 0xff00u) | (x >> 24);
}

uint16_t ntohs(uint16_t x)
{
  return htons(x);
}

uint32_t ntohl(uint32_t x)
{
  return htonl(x);
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
  uint64_t us;
  uint64_t deadline;

  if (req == NULL) {
    return -1;
  }
  us = (uint64_t)req->tv_sec * 1000000ull + (uint64_t)req->tv_nsec / 1000ull;
  if (us == 0u) {
    us = 1u;
  }
  deadline = pm_metal_async_mono_us() + us;
  while (pm_metal_async_mono_us() < deadline) {
  }
  if (rem != NULL) {
    rem->tv_sec = 0;
    rem->tv_nsec = 0;
  }
  return 0;
}

__attribute__((noreturn)) void pm_metal_assert_fail(void)
{
  pm_metal_log((const uint8_t *)"sshd: assert fail\n");
  for (;;) {
  }
}
