/*
 * Symbol export — public API always on; kernel API gated by PM_METAL_BUILD_KERNEL.
 *
 * impl: none — header-only (API visibility macros)
 */
#ifndef PYMERGETIC_METAL_EXPORT_H_
#define PYMERGETIC_METAL_EXPORT_H_

#include <pymergetic/metal/build.h>

#ifndef PM_METAL_MAX_VIS
#define PM_METAL_MAX_VIS 1
#endif

#define PM_METAL_VIS_RUNTIME 1
#define PM_METAL_VIS_DEBUG 2

#define PM_METAL_API(ret, name, args) ret name args

#if defined(PM_METAL_BUILD_KERNEL)
#define PM_METAL_KERNEL_API(ret, name, args) PM_METAL_API(ret, name, args)
#else
#define PM_METAL_KERNEL_API(ret, name, args)
#endif

#endif /* PYMERGETIC_METAL_EXPORT_H_ */
