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
#define PM_METAL_HWINFO_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_HWINFO_WASI_MODULE, name)

/** Print metal DT, backends, and PCI net/virtio scan to host log. */
extern void pm_metal_hwinfo_print(void)
	PM_METAL_HWINFO_IMPORT(pm_metal_hwinfo_print);
#else
/** Print metal DT, backends, and PCI net/virtio scan via pm_metal_logf. */
void pm_metal_hwinfo_print(void);

int pm_metal_hwinfo_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_HWINFO_HWINFO_H_ */
