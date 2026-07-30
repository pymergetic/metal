/** Prefixed before compiling vendored external/tlsf. */
#ifndef PM_METAL_MEM__TLSF_SHIM_H_
#define PM_METAL_MEM__TLSF_SHIM_H_

#include <stddef.h>

__attribute__((noreturn)) void pm_metal_mem_tlsf_assert_fail(void);

#ifndef tlsf_assert
#define tlsf_assert(expr) ((void)((expr) ? 0 : (pm_metal_mem_tlsf_assert_fail(), 0)))
#endif

#endif /* PM_METAL_MEM__TLSF_SHIM_H_ */
