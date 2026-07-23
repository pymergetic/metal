/** @file
  mbedTLS platform hooks for Metal EFI (no libc explicit_bzero).
  (impl: efi|bios)
**/
#include <pymergetic/metal/dev/net/mbedtls_metal_config.h>
#include <runtime/mem/mem.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#include <mbedtls/build_info.h>
#include <mbedtls/platform.h>
#include <mbedtls/platform_util.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

STATIC INT32  mMbedtlsMemReady;

STATIC
VOID *
MetalMbedtlsCalloc (
  size_t  n,
  size_t  sz
  )
{
  size_t  t;
  VOID   *p;

  t = n * sz;
  if (t == 0) {
    return NULL;
  }

  p = pm_metal_mem_alloc (t, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (p != NULL) {
    ZeroMem (p, t);
  }

  return p;
}

STATIC
VOID
MetalMbedtlsFree (
  VOID  *p
  )
{
  pm_metal_mem_free (p);
}

VOID
pm_metal_mbedtls_runtime_init (
  VOID
  )
{
  if (mMbedtlsMemReady) {
    return;
  }

  mbedtls_platform_set_calloc_free (MetalMbedtlsCalloc, MetalMbedtlsFree);
  mMbedtlsMemReady = 1;
}

VOID
mbedtls_platform_zeroize (
  VOID   *buf,
  size_t  len
  )
{
  if (buf != NULL && len > 0) {
    ZeroMem (buf, len);
  }
}

int
mbedtls_metal_snprintf (
  char        *s,
  size_t       n,
  const char  *fmt,
  ...
  )
{
  va_list  args;
  int      w;

  if (s == NULL || n == 0 || fmt == NULL) {
    return -1;
  }

  va_start (args, fmt);
  w = vsnprintf (s, n, fmt, args);
  va_end (args);
  return w;
}

int
mbedtls_metal_vsnprintf (
  char        *s,
  size_t       n,
  const char  *fmt,
  va_list      ap
  )
{
  if (s == NULL || n == 0 || fmt == NULL) {
    return -1;
  }

  /* C %s semantics — not EDK2 AsciiVSPrint (CHAR16* %s, ms_abi va_list). */
  return vsnprintf (s, n, fmt, ap);
}
