/** Compile vendored Conte TLSF 3.1 (external/tlsf) into metal_mem. */
#include "_shim.h"

#include <assert.h>

#ifdef assert
#undef assert
#endif
#define assert(expr) tlsf_assert(expr)

#ifdef static_assert
#undef static_assert
#endif
#define static_assert tlsf_sa_typedef

#ifndef PM_METAL_TLSF_FREESTANDING
#include <stdlib.h>
#endif

__attribute__((noreturn)) void pm_metal_mem_tlsf_assert_fail(void)
{
#ifdef PM_METAL_TLSF_FREESTANDING
  for (;;) {
  }
#else
  abort();
#endif
}

#include "tlsf.c"
