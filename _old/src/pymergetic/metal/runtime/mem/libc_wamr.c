/** @file
  Extra freestanding libc bits required to link WAMR under EDK2.
**/
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

__attribute__((noreturn)) void pm_metal_assert_fail(void)
{
  for (;;) {
  }
}

/* Legacy symbol — prefer assert() macro in host_stubs/assert.h. */
void assert(int cond)
{
  if (!cond) {
    pm_metal_assert_fail();
  }
}

long labs(long x)
{
  return x < 0 ? -x : x;
}

int sched_yield(void)
{
  return 0;
}

int clock_nanosleep(clockid_t              clock_id,
                    int                    flags,
                    const struct timespec *request,
                    struct timespec       *remain)
{
  (void)clock_id;
  (void)flags;
  (void)request;
  (void)remain;
  errno = ENOSYS;
  return -1;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
  (void)req;
  (void)rem;
  errno = ENOSYS;
  return -1;
}

int poll(struct pollfd *pfds, nfds_t nfds, int timeout)
{
  (void)pfds;
  (void)nfds;
  (void)timeout;
  errno = ENOSYS;
  return -1;
}

int pthread_cond_timedwait(pthread_cond_t        *cond,
                           pthread_mutex_t       *mutex,
                           const struct timespec *abstime)
{
  (void)cond;
  (void)mutex;
  (void)abstime;
  return -1;
}

int fputs(const char *s, FILE *f)
{
  (void)f;
  (void)s;
  return 0;
}

static uint16_t MetalSwap16(uint16_t x)
{
  return (uint16_t)((x << 8) | (x >> 8));
}

static uint32_t MetalSwap32(uint32_t x)
{
  return ((x & 0x000000fful) << 24) | ((x & 0x0000ff00ul) << 8) | ((x & 0x00ff0000ul) >> 8) |
         ((x & 0xff000000ul) >> 24);
}

uint16_t htons(uint16_t x)
{
  return MetalSwap16(x);
}

uint32_t htonl(uint32_t x)
{
  return MetalSwap32(x);
}

uint16_t ntohs(uint16_t x)
{
  return MetalSwap16(x);
}

uint32_t ntohl(uint32_t x)
{
  return MetalSwap32(x);
}

size_t strnlen(const char *s, size_t maxlen)
{
  size_t n;

  if (s == NULL) {
    return 0;
  }

  for (n = 0; n < maxlen && s[n] != '\0'; n++) {
  }

  return n;
}

size_t strcspn(const char *s, const char *reject)
{
  size_t i;

  for (i = 0; s[i] != '\0'; i++) {
    const char *r;

    for (r = reject; *r != '\0'; r++) {
      if (s[i] == *r) {
        return i;
      }
    }
  }

  return i;
}

size_t strspn(const char *s, const char *accept)
{
  size_t i;

  for (i = 0; s[i] != '\0'; i++) {
    const char *a;
    int32_t     ok;

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

char *strtok(char *str, const char *delim)
{
  static char *save;
  char        *start;
  char        *p;

  if (str != NULL) {
    save = str;
  }

  if (save == NULL || *save == '\0') {
    return NULL;
  }

  start = save + strspn(save, delim);
  if (*start == '\0') {
    save = start;
    return NULL;
  }

  p = start + strcspn(start, delim);
  if (*p != '\0') {
    *p   = '\0';
    save = p + 1;
  } else {
    save = p;
  }

  return start;
}

static int32_t MetalQsortCmp(const void *a, const void *b, void *arg)
{
  int32_t (*cmp)(const void *, const void *);

  cmp = (int32_t(*)(const void *, const void *))(uintptr_t)arg;
  return cmp(a, b);
}

/* Minimal qsort / bsearch for WAMR symbol tables. */
void qsort(void *base, size_t nmemb, size_t size, int32_t (*compar)(const void *, const void *))
{
  uint8_t *b;
  uint8_t  tmp[256];
  size_t   i;
  size_t   j;

  if (size == 0 || size > sizeof(tmp)) {
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

  (void)MetalQsortCmp;
}

void *bsearch(const void *key,
              const void *base,
              size_t      nmemb,
              size_t      size,
              int32_t (*compar)(const void *, const void *))
{
  size_t lo;
  size_t hi;

  if (size == 0) {
    return NULL;
  }

  lo = 0;
  hi = nmemb;
  while (lo < hi) {
    size_t         mid;
    const uint8_t *p;
    int32_t        c;

    mid = lo + (hi - lo) / 2;
    p   = (const uint8_t *)base + mid * size;
    c   = compar(key, p);
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
