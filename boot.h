/*
 * Metal image boot (unix MICROPY_PY_METAL=1). Hosted fill + wasm malloc.
 * Bring-up is pymergetic.metal.boot. Weak no-op lives in wasmmod ports/common/boot.c.
 */
#ifndef PYMERGETIC_METAL_BOOT_PORT_H
#define PYMERGETIC_METAL_BOOT_PORT_H

#include <stddef.h>

#include "pymergetic/metal/boot/__exports__.h"

#ifdef __cplusplus
extern "C" {
#endif

void pm_metal_upy_port_init(void);

/* The Python renderer behind the httpd's deferred page routes (modmetal.c).
 * `start` is what m.serve() calls; `autostart` marks it to be started from the
 * MOTD surface, for a seat that brought its listeners up on its own. */
void mp_metal_packs_start(void);
void mp_metal_packs_autostart(void);

void *pm_metal_wasm_malloc(size_t n);
void pm_metal_wasm_free(void *p);
void *pm_metal_wasm_realloc(void *p, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_PORT_H */
