#ifndef PM_METAL_MEM_H_
#define PM_METAL_MEM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Seed dual-span arena + TLSF (high side) from a claimed RAM window. */
int32_t pm_metal_mem_init(uint8_t *base, size_t bytes);

uint8_t *pm_metal_mem_alloc(size_t size);
void pm_metal_mem_free(uint8_t *ptr);
/* Like free, but reports retaddr when ptr is outside the TLSF arena. */
void pm_metal_mem_free_checked(uint8_t *ptr, const void *retaddr);
uint8_t *pm_metal_mem_memalign(size_t align, size_t size);
uint8_t *pm_metal_mem_realloc(uint8_t *ptr, size_t size);

/* High-side TLSF carve size (durable heap). */
size_t pm_metal_mem_heap_bytes(void);
size_t pm_metal_mem_free_bytes(void);

/* Dual-span queries (boot tree: area → map | hole | tlsf). */
uintptr_t pm_metal_mem_base(void);
size_t pm_metal_mem_span_bytes(void);
size_t pm_metal_mem_map_used(void);
size_t pm_metal_mem_hole(void);
/* Grow map side upward from the low brk; NULL if hole too small. */
uint8_t *pm_metal_mem_map(size_t bytes);

/* Wasm guest cookies — NULL/0 until guest map is product-linked. */
uint8_t *pm_metal_mem_guest_ptr(uint32_t cookie);
uint32_t pm_metal_mem_guest_size(uint32_t cookie);

#ifdef __cplusplus
}
#endif

#endif
