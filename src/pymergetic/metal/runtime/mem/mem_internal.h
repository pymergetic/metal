/*
 * Host-only mem engine bits — not for guest / fiber authors.
 * Authors use include/.../runtime/mem/mem.h (pm_metal_mem_alloc / free).
 */
#ifndef METAL_RUNTIME_MEM_INTERNAL_H_
#define METAL_RUNTIME_MEM_INTERNAL_H_

#include <pymergetic/metal/runtime/mem/mem.h>

#include <stdint.h>

/** Resolve a guest opaque cookie from pm_metal_mem_alloc (NULL if invalid). */
pm_metal_ptr_t pm_metal_mem_guest_ptr(pm_metal_ptr_t cookie);
/** Byte size of a guest cookie allocation (0 if invalid). */
uint32_t pm_metal_mem_guest_size(pm_metal_ptr_t cookie);

/** Register WASI imports (pm_metal_mem_alloc / free / copy_*). */
int pm_metal_mem_native_register(void);

#endif /* METAL_RUNTIME_MEM_INTERNAL_H_ */
