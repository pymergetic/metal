/*
 * Host hardware inventory — shell + guest dual ABI.
 *
 * impl: common — src/pymergetic/metal/shell/hwinfo/hwinfo.c
 */
#ifndef PYMERGETIC_METAL_SHELL_HWINFO_HWINFO_H_
#define PYMERGETIC_METAL_SHELL_HWINFO_HWINFO_H_

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_HWINFO_WASI_MODULE "pymergetic.metal.hwinfo"

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_HWINFO_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_HWINFO_WASI_MODULE, name)

/** Print metal DT, backends, and PCI net/virtio scan to host log. */
extern void pm_metal_hwinfo_print(void) PM_METAL_HWINFO_IMPORT(pm_metal_hwinfo_print);
#else
#include <stddef.h>

/** Print metal DT, backends, and PCI net/virtio scan via pm_metal_logf. */
void pm_metal_hwinfo_print(void);

/**
 * CPUID brand string ("Intel(R) Core(TM) i7-8550U CPU @ 1.80GHz",
 * "QEMU Virtual CPU version 2.5+", ...), trimmed + NUL-terminated into
 * out (capacity cap). Host-only, no port split needed: CPUID is a plain
 * x86/x86_64 instruction, not a firmware call (same rationale as
 * runtime/time/cpu.h's rdtsc). Falls back to the 12-byte vendor ID
 * ("GenuineIntel", "TCGTCGTCGTCG", ...) on CPUs with no brand-string
 * leaf; out[0] == '\0' only if out/cap are bad.
 */
void pm_metal_hwinfo_cpu_brand(char *out, size_t cap);

int pm_metal_hwinfo_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_HWINFO_HWINFO_H_ */
