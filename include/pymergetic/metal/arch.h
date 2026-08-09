/*
 * Build-time arch / firmware face (one gate for boot tree + arch.current()).
 *
 * Boards / port/webassembly / port/unix set exactly one CFG arch flag:
 *   -DPM_METAL_CFG_ARCH_X86=1
 *   -DPM_METAL_CFG_ARCH_X86_64=1
 *   -DPM_METAL_CFG_ARCH_WASM=1
 * and one firmware face:
 *   -DPM_METAL_CFG_FW_BIOS=1 | _UEFI=1 | _BROWSER=1 | _UNIX=1
 *
 * FW_UNIX is a userspace face (curl-and-run Linux µPy), not freestanding.
 *
 * Enum values use _ID_ / _FW_ so they never collide with the CFG macros.
 */
#ifndef PYMERGETIC_METAL_ARCH_H_
#define PYMERGETIC_METAL_ARCH_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pm_metal_arch_id {
    PM_METAL_ARCH_ID_X86 = 0,
    PM_METAL_ARCH_ID_X86_64 = 1,
    PM_METAL_ARCH_ID_WASM = 2,
} pm_metal_arch_id_t;

typedef enum pm_metal_arch_firmware {
    PM_METAL_FW_ID_NONE = 0,
    PM_METAL_FW_ID_BIOS = 1,
    PM_METAL_FW_ID_UEFI = 2,
    PM_METAL_FW_ID_BROWSER = 3,
    PM_METAL_FW_ID_UNIX = 4, /* userspace host seat */
} pm_metal_arch_firmware_t;

pm_metal_arch_id_t pm_metal_arch_current(void);
const char *pm_metal_arch_name(pm_metal_arch_id_t id);
pm_metal_arch_firmware_t pm_metal_arch_firmware(void);

/*
 * Into-Py bridges (frozen pymergetic.metal.arch). Soft-fail: -1 / empty buf.
 * Seat packs remain compile-time CFG above.
 */
int32_t pm_metal_arch_py_name(char *buf, size_t buf_len);
int32_t pm_metal_arch_py_names(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_ARCH_H_ */
