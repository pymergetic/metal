/* mbedTLS platform hooks for Metal (freestanding). */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "mbedtls_metal_config.h"
#include <mbedtls/build_info.h>
#include <mbedtls/platform.h>

extern void *pm_metal_mem_alloc(size_t size);
extern void pm_metal_mem_free(void *p);

static int32_t g_ready;

static void *metal_calloc(size_t n, size_t sz)
{
  size_t t;
  void *p;

  if (n == 0 || sz == 0) {
    return NULL;
  }
  t = n * sz;
  if (t / sz != n) {
    return NULL;
  }
  p = pm_metal_mem_alloc(t);
  if (p != NULL) {
    memset(p, 0, t);
  }
  return p;
}

static void metal_free(void *p)
{
  pm_metal_mem_free(p);
}

void pm_metal_net_ip_ssl_mbedtls_runtime_init(void)
{
  if (g_ready) {
    return;
  }
  mbedtls_platform_set_calloc_free(metal_calloc, metal_free);
  g_ready = 1;
}

void mbedtls_platform_zeroize(void *buf, size_t len)
{
  if (buf != NULL && len > 0) {
    memset(buf, 0, len);
  }
}

int mbedtls_metal_snprintf(char *s, size_t n, const char *fmt, ...)
{
  va_list args;
  int w;

  if (s == NULL || n == 0 || fmt == NULL) {
    return -1;
  }
  va_start(args, fmt);
  w = vsnprintf(s, n, fmt, args);
  va_end(args);
  return w;
}

int mbedtls_metal_vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
  if (s == NULL || n == 0 || fmt == NULL) {
    return -1;
  }
  return vsnprintf(s, n, fmt, ap);
}
