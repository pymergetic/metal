/** @file
  Prefixed before compiling vendored tlsf (see tlsf_edk2.c).
**/
#ifndef METAL_TLSF_SHIM_H_
#define METAL_TLSF_SHIM_H_

#include <Uefi.h>
#include <stddef.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#ifndef tlsf_assert
#define tlsf_assert(expr)  ASSERT (expr)
#endif

#ifndef assert
#define assert(expr)  ASSERT (expr)
#endif

int printf (const char *fmt, ...);
void *memcpy (void *dst, const void *src, size_t n);
void *memset (void *dst, int c, size_t n);

#endif /* METAL_TLSF_SHIM_H_ */
