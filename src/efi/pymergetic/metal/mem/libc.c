/** @file
  Minimal libc bits for vendored tlsf / WAMR under EDK2.
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

#include <mem/mem.h>

int errno;

int
printf (
  const char  *fmt,
  ...
  )
{
  (VOID)fmt;
  return 0;
}

/*
 * C printf → buffer. Do NOT use EDK2 AsciiVSPrint here: its %s is CHAR16*,
 * while WAMR/libc expect CHAR8* (%s). Mis-parse caused host #PF (CR2≈0x30…).
 */
int
vsnprintf (
  char         *buf,
  size_t        n,
  const char   *fmt,
  va_list       ap
  )
{
  size_t  o;
  size_t  i;

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
    char  c;

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

    /* Skip flags / width / precision / length (enough for WAMR logs). */
    while (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#'
           || fmt[i] == '0')
    {
      i++;
    }

    if (fmt[i] == '*') {
      (VOID)va_arg (ap, int);
      i++;
    } else {
      while (fmt[i] >= '0' && fmt[i] <= '9') {
        i++;
      }
    }

    if (fmt[i] == '.') {
      i++;
      if (fmt[i] == '*') {
        (VOID)va_arg (ap, int);
        i++;
      } else {
        while (fmt[i] >= '0' && fmt[i] <= '9') {
          i++;
        }
      }
    }

    {
      int  long_cnt;

      long_cnt = 0;
      while (fmt[i] == 'h' || fmt[i] == 'l' || fmt[i] == 'L' || fmt[i] == 'z'
             || fmt[i] == 'j' || fmt[i] == 't' || fmt[i] == 'q')
      {
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
        case 's':
        {
          CONST CHAR8  *s;
          size_t        k;

          s = va_arg (ap, CONST CHAR8 *);
          if (s == NULL) {
            s = "(null)";
          }

          for (k = 0; s[k] != '\0' && o + 1 < n; k++) {
            buf[o++] = s[k];
          }

          break;
        }

        case 'c':
          buf[o++] = (CHAR8)va_arg (ap, int);
          break;

        case 'd':
        case 'i':
        {
          CHAR8  tmp[32];
          UINTN  len;
          INT64  v;
          size_t k;

          if (long_cnt >= 2) {
            v = (INT64)va_arg (ap, long long);
          } else if (long_cnt == 1) {
            v = (INT64)va_arg (ap, long);
          } else {
            v = (INT64)va_arg (ap, int);
          }

          len = AsciiSPrint (tmp, sizeof (tmp), "%ld", v);
          for (k = 0; k < (size_t)len && o + 1 < n; k++) {
            buf[o++] = tmp[k];
          }

          break;
        }

        case 'u':
        case 'x':
        case 'X':
        case 'p':
        {
          CHAR8         tmp[32];
          UINTN         len;
          UINT64        v;
          CONST CHAR8  *edk;
          size_t        k;

          if (c == 'p') {
            v   = (UINT64)(UINTN)va_arg (ap, VOID *);
            edk = "0x%lx";
          } else if (long_cnt >= 2) {
            v   = (UINT64)va_arg (ap, unsigned long long);
            edk = (c == 'x') ? "%lx" : (c == 'X') ? "%lX" : "%lu";
          } else if (long_cnt == 1) {
            v   = (UINT64)va_arg (ap, unsigned long);
            edk = (c == 'x') ? "%lx" : (c == 'X') ? "%lX" : "%lu";
          } else {
            v   = (UINT64)va_arg (ap, unsigned int);
            edk = (c == 'x') ? "%x" : (c == 'X') ? "%X" : "%u";
          }

          len = AsciiSPrint (tmp, sizeof (tmp), edk, v);
          for (k = 0; k < (size_t)len && o + 1 < n; k++) {
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

int
snprintf (
  char        *buf,
  size_t       n,
  const char  *fmt,
  ...
  )
{
  va_list  ap;
  int      ret;

  va_start (ap, fmt);
  ret = vsnprintf (buf, n, fmt, ap);
  va_end (ap);
  return ret;
}

void *
memcpy (
  void        *dst,
  const void  *src,
  size_t       n
  )
{
  return CopyMem (dst, (VOID *)(UINTN)src, (UINTN)n);
}

void *
memmove (
  void        *dst,
  const void  *src,
  size_t       n
  )
{
  return CopyMem (dst, (VOID *)(UINTN)src, (UINTN)n);
}

void *
memset (
  void   *dst,
  int     c,
  size_t  n
  )
{
  return SetMem (dst, (UINTN)n, (UINT8)c);
}

int
memcmp (
  const void  *a,
  const void  *b,
  size_t       n
  )
{
  return (int)CompareMem (a, b, (UINTN)n);
}

size_t
strlen (
  const char  *s
  )
{
  return (size_t)AsciiStrLen (s);
}

char *
strcpy (
  char        *dst,
  const char  *src
  )
{
  char  *d = dst;

  while ((*d++ = *src++) != '\0') {
  }

  return dst;
}

char *
strncpy (
  char        *dst,
  const char  *src,
  size_t       n
  )
{
  size_t  i;

  for (i = 0; i < n && src[i] != '\0'; i++) {
    dst[i] = src[i];
  }

  for (; i < n; i++) {
    dst[i] = '\0';
  }

  return dst;
}

int
strcmp (
  const char  *a,
  const char  *b
  )
{
  return (int)AsciiStrCmp (a, b);
}

int
strncmp (
  const char  *a,
  const char  *b,
  size_t       n
  )
{
  return (int)AsciiStrnCmp (a, b, (UINTN)n);
}

char *
strcat (
  char        *dst,
  const char  *src
  )
{
  char  *d = dst + strlen (dst);

  while ((*d++ = *src++) != '\0') {
  }

  return dst;
}

char *
strncat (
  char        *dst,
  const char  *src,
  size_t       n
  )
{
  char    *d = dst + strlen (dst);
  size_t   i;

  for (i = 0; i < n && src[i] != '\0'; i++) {
    d[i] = src[i];
  }

  d[i] = '\0';
  return dst;
}

char *
strchr (
  const char  *s,
  int          c
  )
{
  for (; *s != '\0'; s++) {
    if ((unsigned char)*s == (unsigned char)c) {
      return (char *)(UINTN)s;
    }
  }

  return (c == 0) ? (char *)(UINTN)s : NULL;
}

char *
strrchr (
  const char  *s,
  int          c
  )
{
  const char  *last = NULL;

  for (; *s != '\0'; s++) {
    if ((unsigned char)*s == (unsigned char)c) {
      last = s;
    }
  }

  if (c == 0) {
    return (char *)(UINTN)s;
  }

  return (char *)(UINTN)last;
}

char *
strstr (
  const char  *haystack,
  const char  *needle
  )
{
  size_t  nlen;
  size_t  i;

  if (needle == NULL || needle[0] == '\0') {
    return (char *)(UINTN)haystack;
  }

  nlen = strlen (needle);
  for (i = 0; haystack[i] != '\0'; i++) {
    if (strncmp (haystack + i, needle, nlen) == 0) {
      return (char *)(UINTN)(haystack + i);
    }
  }

  return NULL;
}

void *
malloc (
  size_t  n
  )
{
  return pm_metal_mem_alloc (n, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
}

void *
realloc (
  void    *p,
  size_t   n
  )
{
  return pm_metal_mem_realloc (p, n);
}

void
free (
  void  *p
  )
{
  pm_metal_mem_free (p);
}

void *
calloc (
  size_t  nmemb,
  size_t  size
  )
{
  size_t  total;
  void   *p;

  if (nmemb != 0 && size > (SIZE_MAX / nmemb)) {
    return NULL;
  }

  total = nmemb * size;
  p     = malloc (total);
  if (p != NULL) {
    SetMem (p, (UINTN)total, 0);
  }

  return p;
}

int
atoi (
  const char  *s
  )
{
  return (int)AsciiStrDecimalToUintn (s);
}

long
atol (
  const char  *s
  )
{
  return (long)AsciiStrDecimalToUintn (s);
}

long
strtol (
  const char  *nptr,
  char       **endptr,
  int          base
  )
{
  (VOID)endptr;
  (VOID)base;
  return (long)AsciiStrDecimalToUintn (nptr);
}

unsigned long
strtoul (
  const char  *nptr,
  char       **endptr,
  int          base
  )
{
  (VOID)endptr;
  (VOID)base;
  return (unsigned long)AsciiStrDecimalToUintn (nptr);
}

unsigned long long
strtoull (
  const char  *nptr,
  char       **endptr,
  int          base
  )
{
  (VOID)endptr;
  (VOID)base;
  return (unsigned long long)AsciiStrHexToUint64 (nptr);
}

double
strtod (
  const char  *nptr,
  char       **endptr
  )
{
  (VOID)nptr;
  if (endptr != NULL) {
    *endptr = (char *)(UINTN)nptr;
  }

  return 0.0;
}

float
strtof (
  const char  *nptr,
  char       **endptr
  )
{
  (VOID)nptr;
  if (endptr != NULL) {
    *endptr = (char *)(UINTN)nptr;
  }

  return 0.0f;
}

void
abort (
  VOID
  )
{
  ASSERT (FALSE);
  CpuDeadLoop ();
}

#include <time.h>
#include <time/time.h>

int
clock_gettime (
  clockid_t         clock_id,
  struct timespec  *tp
  )
{
  UINT64  us;

  (VOID)clock_id;
  if (tp == NULL) {
    return -1;
  }

  us = pm_metal_time_mono_us ();
  tp->tv_sec  = (time_t)(us / 1000000ull);
  tp->tv_nsec = (long)((us % 1000000ull) * 1000ull);
  return 0;
}

#include <stdio.h>

static FILE g_stdin;
static FILE g_stdout;
static FILE g_stderr;
FILE *stdin = &g_stdin;
FILE *stdout = &g_stdout;
FILE *stderr = &g_stderr;

int
fprintf (
  FILE        *f,
  const char  *fmt,
  ...
  )
{
  (VOID)f;
  (VOID)fmt;
  return 0;
}

int
fflush (
  FILE  *f
  )
{
  (VOID)f;
  return 0;
}

int
ioctl (
  int             fd,
  unsigned long   request,
  ...
  )
{
  (VOID)fd;
  (VOID)request;
  return -1;
}

void
arc4random_buf (
  void   *buf,
  size_t  nbytes
  )
{
  UINT8   *p;
  UINT64  x;
  size_t  i;

  p = (UINT8 *)buf;
  x = pm_metal_time_mono_us ();
  for (i = 0; i < nbytes; i++) {
    x = x * 6364136223846793005ull + 1ull;
    p[i] = (UINT8)(x >> 33);
  }
}
