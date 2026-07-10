/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Shared userspace allocator micro-benchmarks (malloc/mmap workloads).
 */

#ifndef PM_METAL_MEMORY_BENCH_H_
#define PM_METAL_MEMORY_BENCH_H_

#include <pymergetic/pm_vis.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if PM_MAX_VIS >= PM_VIS_RUNTIME

typedef uint64_t (*pm_metal_bench_now_ns_fn)(void);

PM_API(PM_VIS_RUNTIME, void, pm_metal_memory_bench_run,
       (pm_metal_bench_now_ns_fn now_ns, const char *platform_label))

#endif /* PM_MAX_VIS >= PM_VIS_RUNTIME */

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_MEMORY_BENCH_H_ */
