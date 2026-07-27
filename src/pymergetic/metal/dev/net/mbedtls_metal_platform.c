/** @file
  mbedTLS platform hooks for Metal EFI (no libc explicit_bzero).
  (impl: efi|bios)
**/
#include <pymergetic/metal/boot/externals.h>
#include <pymergetic/metal/dev/net/mbedtls_metal_config.h>
#include <pymergetic/metal/runtime/mem/mem.h>

#include <mbedtls/build_info.h>
#include <mbedtls/platform.h>
#include <mbedtls/platform_util.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t mMbedtlsMemReady;

static void *MetalMbedtlsCalloc(size_t n, size_t sz)
{
  size_t t;
  void  *p;

  t = n * sz;
  if (t == 0) {
    return NULL;
  }

  p = pm_metal_mem_alloc(t, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (p != NULL) {
    memset(p, 0, t);
  }

  return p;
}

static void MetalMbedtlsFree(void *p)
{
  pm_metal_mem_free(p);
}

void pm_metal_mbedtls_runtime_init(void)
{
  if (mMbedtlsMemReady) {
    return;
  }

  mbedtls_platform_set_calloc_free(MetalMbedtlsCalloc, MetalMbedtlsFree);
  mMbedtlsMemReady = 1;
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
  int     w;

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

  /* C %s semantics — not EDK2 AsciiVSPrint (CHAR16* %s, ms_abi va_list). */
  return vsnprintf(s, n, fmt, ap);
}

PM_METAL_EXTERNAL(g_pm_metal_ext_mbedtls,
                  mbedtls,
                  MBEDTLS_VERSION_STRING,
                  "https://github.com/Mbed-TLS/mbedtls",
                  "TLS / crypto");
