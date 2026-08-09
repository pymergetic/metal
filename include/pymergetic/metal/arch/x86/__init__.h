#ifndef PM_METAL_ARCH_X86_H_
#define PM_METAL_ARCH_X86_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Into-Py face for frozen pymergetic.metal.arch.x86 (API 3).
 * Soft-fail: -1 / empty buf.
 */

int32_t pm_metal_arch_x86_name(char *buf, size_t buf_len);
int32_t pm_metal_arch_x86_firmware(char *buf, size_t buf_len);
int32_t pm_metal_arch_x86_autoexec(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_ARCH_X86_H_ */
