/** @file
  Extra freestanding libc bits required to link WAMR under EDK2.
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include <errno.h>

#include <runtime/time/time.h>

void
assert (
  int  cond
  )
{
  if (!cond) {
    for (;;) {
    }
  }
}

long
labs (
  long  x
  )
{
  return x < 0 ? -x : x;
}

int
sched_yield (
  VOID
  )
{
  return 0;
}

int
clock_nanosleep (
  clockid_t               clock_id,
  int                     flags,
  CONST struct timespec  *request,
  struct timespec        *remain
  )
{
  (VOID)clock_id;
  (VOID)flags;
  (VOID)request;
  (VOID)remain;
  errno = ENOSYS;
  return -1;
}

int
nanosleep (
  CONST struct timespec  *req,
  struct timespec        *rem
  )
{
  (VOID)req;
  (VOID)rem;
  errno = ENOSYS;
  return -1;
}

int
poll (
  struct pollfd  *pfds,
  nfds_t          nfds,
  int             timeout
  )
{
  (VOID)pfds;
  (VOID)nfds;
  (VOID)timeout;
  errno = ENOSYS;
  return -1;
}

int
pthread_cond_timedwait (
  pthread_cond_t         *cond,
  pthread_mutex_t        *mutex,
  CONST struct timespec  *abstime
  )
{
  (VOID)cond;
  (VOID)mutex;
  (VOID)abstime;
  return -1;
}

int
fputs (
  CONST CHAR8  *s,
  FILE         *f
  )
{
  (VOID)f;
  (VOID)s;
  return 0;
}

uint16_t
htons (
  uint16_t  x
  )
{
  return SwapBytes16 (x);
}

uint32_t
htonl (
  uint32_t  x
  )
{
  return SwapBytes32 (x);
}

uint16_t
ntohs (
  uint16_t  x
  )
{
  return SwapBytes16 (x);
}

uint32_t
ntohl (
  uint32_t  x
  )
{
  return SwapBytes32 (x);
}

size_t
strnlen (
  CONST CHAR8  *s,
  size_t        maxlen
  )
{
  size_t  n;

  if (s == NULL) {
    return 0;
  }

  for (n = 0; n < maxlen && s[n] != '\0'; n++) {
  }

  return n;
}

size_t
strcspn (
  CONST CHAR8  *s,
  CONST CHAR8  *reject
  )
{
  size_t  i;

  for (i = 0; s[i] != '\0'; i++) {
    CONST CHAR8  *r;

    for (r = reject; *r != '\0'; r++) {
      if (s[i] == *r) {
        return i;
      }
    }
  }

  return i;
}

size_t
strspn (
  CONST CHAR8  *s,
  CONST CHAR8  *accept
  )
{
  size_t  i;

  for (i = 0; s[i] != '\0'; i++) {
    CONST CHAR8  *a;
    INT32         ok;

    ok = 0;
    for (a = accept; *a != '\0'; a++) {
      if (s[i] == *a) {
        ok = 1;
        break;
      }
    }

    if (!ok) {
      return i;
    }
  }

  return i;
}

CHAR8 *
strtok (
  CHAR8        *str,
  CONST CHAR8  *delim
  )
{
  STATIC CHAR8  *save;
  CHAR8         *start;
  CHAR8         *p;

  if (str != NULL) {
    save = str;
  }

  if (save == NULL || *save == '\0') {
    return NULL;
  }

  start = save + strspn (save, delim);
  if (*start == '\0') {
    save = start;
    return NULL;
  }

  p = start + strcspn (start, delim);
  if (*p != '\0') {
    *p = '\0';
    save = p + 1;
  } else {
    save = p;
  }

  return start;
}

STATIC
INT32
MetalQsortCmp (
  CONST VOID  *a,
  CONST VOID  *b,
  VOID        *arg
  )
{
  INT32  (*cmp)(CONST VOID *, CONST VOID *);

  cmp = (INT32 (*)(CONST VOID *, CONST VOID *))(UINTN)arg;
  return cmp (a, b);
}

/* Minimal qsort / bsearch for WAMR symbol tables. */
VOID
qsort (
  VOID    *base,
  size_t   nmemb,
  size_t   size,
  INT32    (*compar)(CONST VOID *, CONST VOID *)
  )
{
  UINT8  *b;
  UINT8   tmp[256];
  size_t  i;
  size_t  j;

  if (size == 0 || size > sizeof (tmp)) {
    return;
  }

  b = (UINT8 *)base;
  for (i = 1; i < nmemb; i++) {
    CopyMem (tmp, b + i * size, size);
    j = i;
    while (j > 0 && compar (tmp, b + (j - 1) * size) < 0) {
      CopyMem (b + j * size, b + (j - 1) * size, size);
      j--;
    }

    CopyMem (b + j * size, tmp, size);
  }

  (VOID)MetalQsortCmp;
}

VOID *
bsearch (
  CONST VOID  *key,
  CONST VOID  *base,
  size_t       nmemb,
  size_t       size,
  INT32        (*compar)(CONST VOID *, CONST VOID *)
  )
{
  size_t  lo;
  size_t  hi;

  if (size == 0) {
    return NULL;
  }

  lo = 0;
  hi = nmemb;
  while (lo < hi) {
    size_t       mid;
    CONST UINT8  *p;
    INT32         c;

    mid = lo + (hi - lo) / 2;
    p   = (CONST UINT8 *)base + mid * size;
    c   = compar (key, p);
    if (c == 0) {
      return (VOID *)(UINTN)p;
    }

    if (c < 0) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }

  return NULL;
}
