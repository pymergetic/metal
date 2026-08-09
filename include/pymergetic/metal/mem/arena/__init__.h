#ifndef PYMERGETIC_METAL_MEM_ARENA_H_
#define PYMERGETIC_METAL_MEM_ARENA_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Matches src/.../mem/arena Arena #[repr(C)]. */
typedef struct pm_metal_mem_arena {
    size_t base;
    size_t end;
    size_t map_brk;
    size_t heap_brk;
    uint32_t lock;
} pm_metal_mem_arena_t;

pm_metal_mem_arena_t pm_metal_mem_arena_empty(void);
int32_t pm_metal_mem_arena_init(pm_metal_mem_arena_t *a, uint8_t *base, size_t bytes);
int32_t pm_metal_mem_arena_ready(const pm_metal_mem_arena_t *a);
size_t pm_metal_mem_arena_bytes(const pm_metal_mem_arena_t *a);
size_t pm_metal_mem_arena_map_used(const pm_metal_mem_arena_t *a);
size_t pm_metal_mem_arena_heap_used(const pm_metal_mem_arena_t *a);
size_t pm_metal_mem_arena_hole(const pm_metal_mem_arena_t *a);
uint8_t *pm_metal_mem_arena_heap_grow(pm_metal_mem_arena_t *a, size_t bytes);
uint8_t *pm_metal_mem_arena_map(pm_metal_mem_arena_t *a, size_t bytes);
int32_t pm_metal_mem_arena_unmap(pm_metal_mem_arena_t *a, uint8_t *ptr, size_t bytes);
size_t pm_metal_mem_arena_align_up(size_t x, size_t a);
size_t pm_metal_mem_arena_align_down(size_t x, size_t a);
size_t pm_metal_mem_arena_page_size(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_MEM_ARENA_H_ */
