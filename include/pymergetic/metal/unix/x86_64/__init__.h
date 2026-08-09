#ifndef PM_METAL_UNIX_X86_64_H_
#define PM_METAL_UNIX_X86_64_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Into-Py face for frozen pymergetic.metal.unix.x86_64 (API 2).
 * Soft-fail: -1 / empty buf.
 */

int32_t pm_metal_unix_x86_64_name(char *buf, size_t buf_len);
int32_t pm_metal_unix_x86_64_autoexec(void);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_UNIX_X86_64_H_ */
