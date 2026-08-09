#ifndef PM_METAL_ARCH_WASM_H_
#define PM_METAL_ARCH_WASM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Into-Py face for frozen pymergetic.metal.arch.wasm (API 3).
 * Soft-fail: -1 / empty buf.
 */

int32_t pm_metal_arch_wasm_name(char *buf, size_t buf_len);
int32_t pm_metal_arch_wasm_firmware(char *buf, size_t buf_len);
int32_t pm_metal_arch_wasm_autoexec(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_ARCH_WASM_H_ */
