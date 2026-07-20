/** @file
  Compile vendored TLSF with EDK2-friendly shims (no host libc link).
**/
#include "tlsf_shim.h" /* IWYU pragma: keep */

/* Pull function-decl assert.h before mapping assert → ASSERT. */
#include <assert.h>

#ifdef assert
#undef assert
#endif
#define assert(expr)  tlsf_assert (expr)

/*
  Rename C11 static_assert so tlsf's token-paste helper works under
  -std=c11 / clangd.
*/
#ifdef static_assert
#undef static_assert
#endif
#define static_assert  tlsf_sa_typedef

#include "tlsf.c"
