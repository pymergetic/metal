/** @file
  mbedTLS platform hooks for Metal EFI (no libc explicit_bzero).
  (impl: efi|bios)
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#include <mbedtls/build_info.h>
#include <mbedtls/platform_util.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

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
