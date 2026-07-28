/** @file
  Minimal libc bits for vendored tlsf / WAMR under EDK2.
**/

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

#include <pymergetic/metal/runtime/mem/mem.h>
#include "host_stubs/assert.h"

int errno;

/* Local number->ascii helpers — vsnprintf must not call snprintf/itself. */
static size_t MetalU64ToStr(char *out, size_t cap, uint64_t v, uint32_t base, bool upper)
{
  static const char lo[]   = "0123456789abcdef";
  static const char hi[]   = "0123456789ABCDEF";
  const char       *digits = upper ? hi : lo;
  char              tmp[32];
  size_t            n;
  size_t            i;

  n = 0;
  if (v == 0) {
    tmp[n++] = '0';
  } else {
    while (v != 0 && n < sizeof(tmp)) {
      tmp[n++] = digits[v % base];
      v /= base;
    }
  }

  if (n > cap) {
    n = cap;
  }

  for (i = 0; i < n; i++) {
    out[i] = tmp[n - 1 - i];
  }

  return n;
}

static size_t MetalI64ToStr(char *out, size_t cap, int64_t v)
{
  uint64_t u;
  size_t   n;

  n = 0;
  if (v < 0) {
    if (cap > 0) {
      out[0] = '-';
    }

    n = 1;
    u = (uint64_t)(-v);
  } else {
    u = (uint64_t)v;
  }

  if (n < cap) {
    n += MetalU64ToStr(out + n, cap - n, u, 10, false);
  }

  return n;
}

int printf(const char *fmt, ...)
{
  (void)fmt;
  return 0;
}

/*
 * C printf → buffer. Do NOT use EDK2 vsnprintf here: its %s is CHAR16*,
 * while WAMR/libc expect char* (%s). Mis-parse caused host #PF (CR2≈0x30…).
 */
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
  size_t o;
  size_t i;

  if (buf == NULL || n == 0) {
    return 0;
  }

  if (fmt == NULL) {
    buf[0] = '\0';
    return 0;
  }

  o = 0;
  i = 0;
  while (fmt[i] != '\0' && o + 1 < n) {
    char c;

    c = fmt[i++];
    if (c != '%') {
      buf[o++] = c;
      continue;
    }

    if (fmt[i] == '%') {
      buf[o++] = '%';
      i++;
      continue;
    }

    /* Flags / width / precision / length (enough for WAMR + microtar). */
    {
      int zero_pad;
      int width;
      int long_cnt;

      zero_pad = 0;
      width    = 0;
      while (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0') {
        if (fmt[i] == '0') {
          zero_pad = 1;
        }

        i++;
      }

      if (fmt[i] == '*') {
        width = va_arg(ap, int);
        i++;
      } else {
        while (fmt[i] >= '0' && fmt[i] <= '9') {
          width = width * 10 + (fmt[i] - '0');
          i++;
        }
      }

      if (fmt[i] == '.') {
        i++;
        if (fmt[i] == '*') {
          (void)va_arg(ap, int);
          i++;
        } else {
          while (fmt[i] >= '0' && fmt[i] <= '9') {
            i++;
          }
        }
      }

      long_cnt = 0;
      while (fmt[i] == 'h' || fmt[i] == 'l' || fmt[i] == 'L' || fmt[i] == 'z' || fmt[i] == 'j' ||
             fmt[i] == 't' || fmt[i] == 'q') {
        if (fmt[i] == 'l') {
          long_cnt++;
        }

        i++;
      }

      c = fmt[i];
      if (c == '\0') {
        break;
      }

      i++;
      switch (c) {
      case 's': {
        const char *s;
        size_t      k;

        s = va_arg(ap, const char *);
        if (s == NULL) {
          s = "(null)";
        }

        for (k = 0; s[k] != '\0' && o + 1 < n; k++) {
          buf[o++] = s[k];
        }

        break;
      }

      case 'c':
        buf[o++] = (char)va_arg(ap, int);
        break;

      case 'd':
      case 'i': {
        char    tmp[32];
        size_t  len;
        int64_t v;
        size_t  k;

        if (long_cnt >= 2) {
          v = (int64_t)va_arg(ap, long long);
        } else if (long_cnt == 1) {
          v = (int64_t)va_arg(ap, long);
        } else {
          v = (int64_t)va_arg(ap, int);
        }

        len = MetalI64ToStr(tmp, sizeof(tmp), v);
        {
          int pad;

          pad = (width > (int)len) ? (width - (int)len) : 0;
          while (pad-- > 0 && o + 1 < n) {
            buf[o++] = zero_pad ? '0' : ' ';
          }
        }

        for (k = 0; k < len && o + 1 < n; k++) {
          buf[o++] = tmp[k];
        }

        break;
      }

      case 'o': {
        char     tmp[32];
        uint64_t v;
        int      digits;
        int      pad;
        int      k;

        if (long_cnt >= 2) {
          v = (uint64_t)va_arg(ap, unsigned long long);
        } else if (long_cnt == 1) {
          v = (uint64_t)va_arg(ap, unsigned long);
        } else {
          v = (uint64_t)va_arg(ap, unsigned int);
        }

        digits = 0;
        if (v == 0) {
          tmp[digits++] = '0';
        } else {
          while (v != 0 && digits < (int)sizeof(tmp)) {
            tmp[digits++] = (char)('0' + (int)(v & 7ull));
            v >>= 3;
          }
        }

        pad = (width > digits) ? (width - digits) : 0;
        if (zero_pad) {
          while (pad-- > 0 && o + 1 < n) {
            buf[o++] = '0';
          }
        } else {
          while (pad-- > 0 && o + 1 < n) {
            buf[o++] = ' ';
          }
        }

        for (k = digits - 1; k >= 0 && o + 1 < n; k--) {
          buf[o++] = tmp[k];
        }

        break;
      }

      case 'u':
      case 'x':
      case 'X':
      case 'p': {
        char     tmp[32];
        size_t   len;
        uint64_t v;
        uint32_t base;
        bool     upper;
        bool     prefix;
        size_t   k;

        upper  = (c == 'X');
        prefix = false;
        if (c == 'p') {
          v      = (uint64_t)(uintptr_t)va_arg(ap, void *);
          base   = 16;
          prefix = true;
        } else if (long_cnt >= 2) {
          v    = (uint64_t)va_arg(ap, unsigned long long);
          base = (c == 'u') ? 10 : 16;
        } else if (long_cnt == 1) {
          v    = (uint64_t)va_arg(ap, unsigned long);
          base = (c == 'u') ? 10 : 16;
        } else {
          v    = (uint64_t)va_arg(ap, unsigned int);
          base = (c == 'u') ? 10 : 16;
        }

        if (prefix && o + 2 < n) {
          buf[o++] = '0';
          buf[o++] = 'x';
        }

        len = MetalU64ToStr(tmp, sizeof(tmp), v, base, upper);
        {
          int pad;

          pad = (width > (int)len) ? (width - (int)len) : 0;
          while (pad-- > 0 && o + 1 < n) {
            buf[o++] = zero_pad ? '0' : ' ';
          }
        }

        for (k = 0; k < len && o + 1 < n; k++) {
          buf[o++] = tmp[k];
        }

        break;
      }

      default:
        if (o + 1 < n) {
          buf[o++] = '%';
        }

        if (o + 1 < n) {
          buf[o++] = c;
        }

        break;
      }
    }
  }

  buf[o] = '\0';
  return (int)o;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
  va_list ap;
  int     ret;

  va_start(ap, fmt);
  ret = vsnprintf(buf, n, fmt, ap);
  va_end(ap);
  return ret;
}

void *memcpy(void *dst, const void *src, size_t n)
{
  uint8_t       *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  size_t         i;

  for (i = 0; i < n; i++) {
    d[i] = s[i];
  }

  return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
  uint8_t       *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  size_t         i;

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

void *memset(void *dst, int c, size_t n)
{
  uint8_t *d = (uint8_t *)dst;
  size_t   i;

  for (i = 0; i < n; i++) {
    d[i] = (uint8_t)c;
  }

  return dst;
}

void *memchr(const void *s, int c, size_t n)
{
  const uint8_t *p;
  size_t         i;

  p = (const uint8_t *)s;
  for (i = 0; i < n; i++) {
    if (p[i] == (uint8_t)c) {
      return (void *)(p + i);
    }
  }
  return NULL;
}

int memcmp(const void *a, const void *b, size_t n)
{
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  size_t         i;

  for (i = 0; i < n; i++) {
    if (pa[i] != pb[i]) {
      return (int)pa[i] - (int)pb[i];
    }
  }

  return 0;
}

size_t strlen(const char *s)
{
  size_t n;

  n = 0;
  while (s[n] != '\0') {
    n++;
  }

  return n;
}

char *strcpy(char *dst, const char *src)
{
  char *d = dst;

  while ((*d++ = *src++) != '\0') {
  }

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

int strcmp(const char *a, const char *b)
{
  while (*a != '\0' && *a == *b) {
    a++;
    b++;
  }

  return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
  size_t i;

  for (i = 0; i < n; i++) {
    if (a[i] != b[i] || a[i] == '\0') {
      return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
    }
  }

  return 0;
}

char *strcat(char *dst, const char *src)
{
  char *d = dst + strlen(dst);

  while ((*d++ = *src++) != '\0') {
  }

  return dst;
}

char *strncat(char *dst, const char *src, size_t n)
{
  char  *d = dst + strlen(dst);
  size_t i;

  for (i = 0; i < n && src[i] != '\0'; i++) {
    d[i] = src[i];
  }

  d[i] = '\0';
  return dst;
}

char *strchr(const char *s, int c)
{
  for (; *s != '\0'; s++) {
    if ((unsigned char)*s == (unsigned char)c) {
      return (char *)(uintptr_t)s;
    }
  }

  return (c == 0) ? (char *)(uintptr_t)s : NULL;
}

char *strrchr(const char *s, int c)
{
  const char *last = NULL;

  for (; *s != '\0'; s++) {
    if ((unsigned char)*s == (unsigned char)c) {
      last = s;
    }
  }

  if (c == 0) {
    return (char *)(uintptr_t)s;
  }

  return (char *)(uintptr_t)last;
}

char *strstr(const char *haystack, const char *needle)
{
  size_t nlen;
  size_t i;

  if (needle == NULL || needle[0] == '\0') {
    return (char *)(uintptr_t)haystack;
  }

  nlen = strlen(needle);
  for (i = 0; haystack[i] != '\0'; i++) {
    if (strncmp(haystack + i, needle, nlen) == 0) {
      return (char *)(uintptr_t)(haystack + i);
    }
  }

  return NULL;
}

void *malloc(size_t n)
{
  return pm_metal_mem_alloc(n, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
}

void *realloc(void *p, size_t n)
{
  return pm_metal_mem_realloc(p, n);
}

void free(void *p)
{
  pm_metal_mem_free(p);
}

void *calloc(size_t nmemb, size_t size)
{
  size_t total;
  void  *p;

  if (nmemb != 0 && size > (SIZE_MAX / nmemb)) {
    return NULL;
  }

  total = nmemb * size;
  p     = malloc(total);
  if (p != NULL) {
    memset(p, 0, (uintptr_t)total);
  }

  return p;
}

int atoi(const char *s)
{
  return (int)strtoul(s, NULL, 10);
}

long atol(const char *s)
{
  return (long)strtoul(s, NULL, 10);
}

long strtol(const char *nptr, char **endptr, int base)
{
  (void)endptr;
  (void)base;
  return (long)strtoul(nptr, NULL, 10);
}

/* Decimal only — base param ignored (matches pre-conversion behaviour). */
unsigned long strtoul(const char *nptr, char **endptr, int base)
{
  unsigned long v;

  (void)base;
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

/* Hex only — base param ignored (matches pre-conversion behaviour). */
unsigned long long strtoull(const char *nptr, char **endptr, int base)
{
  unsigned long long v;

  (void)base;
  while (*nptr == ' ' || *nptr == '\t') {
    nptr++;
  }

  if (nptr[0] == '0' && (nptr[1] == 'x' || nptr[1] == 'X')) {
    nptr += 2;
  }

  v = 0;
  for (;; nptr++) {
    unsigned long long d;

    if (*nptr >= '0' && *nptr <= '9') {
      d = (unsigned long long)(*nptr - '0');
    } else if (*nptr >= 'a' && *nptr <= 'f') {
      d = (unsigned long long)(*nptr - 'a') + 10ull;
    } else if (*nptr >= 'A' && *nptr <= 'F') {
      d = (unsigned long long)(*nptr - 'A') + 10ull;
    } else {
      break;
    }

    v = v * 16ull + d;
  }

  if (endptr != NULL) {
    *endptr = (char *)(uintptr_t)nptr;
  }

  return v;
}

double strtod(const char *nptr, char **endptr)
{
  (void)nptr;
  if (endptr != NULL) {
    *endptr = (char *)(uintptr_t)nptr;
  }

  return 0.0;
}

float strtof(const char *nptr, char **endptr)
{
  (void)nptr;
  if (endptr != NULL) {
    *endptr = (char *)(uintptr_t)nptr;
  }

  return 0.0f;
}

void abort(void)
{
  pm_metal_assert_fail();
}

#include <time.h>
#include <runtime/time/time.h>

int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
  uint64_t us;

  (void)clock_id;
  if (tp == NULL) {
    return -1;
  }

  us          = pm_metal_time_mono_us();
  tp->tv_sec  = (time_t)(us / 1000000ull);
  tp->tv_nsec = (long)((us % 1000000ull) * 1000ull);
  return 0;
}

#include <stdio.h>

static FILE g_stdin;
static FILE g_stdout;
static FILE g_stderr;
FILE       *stdin  = &g_stdin;
FILE       *stdout = &g_stdout;
FILE       *stderr = &g_stderr;

int fprintf(FILE *f, const char *fmt, ...)
{
  (void)f;
  (void)fmt;
  return 0;
}

int fflush(FILE *f)
{
  (void)f;
  return 0;
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
  return -1;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *f)
{
  (void)ptr;
  (void)size;
  (void)nmemb;
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

int fseek(FILE *f, long offset, int whence)
{
  (void)f;
  (void)offset;
  (void)whence;
  return -1;
}

long ftell(FILE *f)
{
  (void)f;
  return -1L;
}

int sprintf(char *buf, const char *fmt, ...)
{
  va_list ap;
  int     n;

  va_start(ap, fmt);
  n = vsnprintf(buf, 512, fmt, ap);
  va_end(ap);
  return n;
}

int sscanf(const char *str, const char *fmt, ...)
{
  unsigned   *out;
  unsigned    v;
  const char *p;
  va_list     ap;

  if (str == NULL || fmt == NULL) {
    return 0;
  }

  /* microtar only needs "%o" → unsigned * */
  if (fmt[0] != '%' || fmt[1] != 'o' || fmt[2] != '\0') {
    return 0;
  }

  va_start(ap, fmt);
  out = va_arg(ap, unsigned *);
  va_end(ap);
  if (out == NULL) {
    return 0;
  }

  p = str;
  while (*p == ' ' || *p == '\t') {
    p++;
  }

  v = 0;
  if (*p < '0' || *p > '7') {
    *out = 0;
    return 0;
  }

  while (*p >= '0' && *p <= '7') {
    v = (v << 3) + (unsigned)(*p - '0');
    p++;
  }

  *out = v;
  return 1;
}

/* Weak: DropbearGlue provides a strong ioctl that handles TTY winsize. */
__attribute__((weak)) /* Weak: DropbearGlue provides a strong ioctl that handles TTY winsize. */
__attribute__((weak)) int
ioctl(int fd, unsigned long request, ...)
{
  (void)fd;
  (void)request;
  return -1;
}

void arc4random_buf(void *buf, size_t nbytes)
{
  uint8_t *p;
  uint64_t x;
  size_t   i;

  p = (uint8_t *)buf;
  x = pm_metal_time_mono_us();
  for (i = 0; i < nbytes; i++) {
    x    = x * 6364136223846793005ull + 1ull;
    p[i] = (uint8_t)(x >> 33);
  }
}
