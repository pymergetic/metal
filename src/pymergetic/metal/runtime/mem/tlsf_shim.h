/** @file
  Prefixed before compiling vendored tlsf (see tlsf_edk2.c).
**/
#ifndef PM_METAL_TLSF_SHIM_H_
#define PM_METAL_TLSF_SHIM_H_

#include <stddef.h>

/* Implementation in libc_wamr.c. Not routed through the assert() macro —
 * tlsf_edk2.c also redefines assert(expr) as tlsf_assert(expr), and both
 * being spelled "assert" underneath would make the two macros expand into
 * each other forever. */
__attribute__((noreturn)) void pm_metal_assert_fail(void);
#ifndef tlsf_assert
#define tlsf_assert(expr) ((void)((expr) ? 0 : (pm_metal_assert_fail(), 0)))
#endif

int   printf(const char *fmt, ...);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);

#endif /* PM_METAL_TLSF_SHIM_H_ */
