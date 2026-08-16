/*
 * Unix MICROPY_PY_METAL=1. Keep µPy GC. Packet / io.fetch bytes on util.mem.
 * Firmware images use ports/metal/mpconfig_metal.h (GC off) instead.
 */
#ifndef PYMERGETIC_METAL_MPCONFIG_UNIX_H
#define PYMERGETIC_METAL_MPCONFIG_UNIX_H

#include <stddef.h>

void *pm_metal_wasm_malloc(size_t n);
void pm_metal_wasm_free(void *p);
void *pm_metal_wasm_realloc(void *p, size_t n);

#ifndef MICROPY_WASM_MALLOC
#define MICROPY_WASM_MALLOC(sz) pm_metal_wasm_malloc(sz)
#define MICROPY_WASM_FREE(p) pm_metal_wasm_free(p)
#define MICROPY_WASM_REALLOC(p, sz) pm_metal_wasm_realloc((p), (sz))
#endif

#endif /* PYMERGETIC_METAL_MPCONFIG_UNIX_H */
