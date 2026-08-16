/*
 * Metal image boot (unix MICROPY_PY_METAL=1). Arena + runners + lo + io_ops.
 * Does not turn µPy GC off. Weak no-op lives in wasmmod ports/common/boot.c.
 */
#ifndef PYMERGETIC_METAL_BOOT_H
#define PYMERGETIC_METAL_BOOT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int pm_metal_boot(void);
int pm_metal_ready(void);

void *pm_metal_wasm_malloc(size_t n);
void pm_metal_wasm_free(void *p);
void *pm_metal_wasm_realloc(void *p, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_H */
