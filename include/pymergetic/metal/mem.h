#ifndef PM_METAL_MEM_H_
#define PM_METAL_MEM_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Seed TLSF from a board-provided RAM window. Returns 0 on success. */
int32_t pm_metal_mem_init(uint8_t *base, size_t bytes);

uint8_t *pm_metal_mem_alloc(size_t size);
void pm_metal_mem_free(uint8_t *ptr);
uint8_t *pm_metal_mem_memalign(size_t align, size_t size);
uint8_t *pm_metal_mem_realloc(uint8_t *ptr, size_t size);

size_t pm_metal_mem_heap_bytes(void);
size_t pm_metal_mem_free_bytes(void);

#ifdef __cplusplus
}
#endif

#endif
