/** @file
  Minimal libc bits for vendored tlsf under EDK2.
**/
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#include <stddef.h>
#include <stdarg.h>

int
printf (
  const char  *fmt,
  ...
  )
{
  (VOID)fmt;
  return 0;
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
memset (
  void   *dst,
  int     c,
  size_t  n
  )
{
  return SetMem (dst, (UINTN)n, (UINT8)c);
}

void
abort (
  VOID
  )
{
  ASSERT (FALSE);
  CpuDeadLoop ();
}
