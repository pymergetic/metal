#ifndef PYMERGETIC_METAL_MEM_TLSF_H_
#define PYMERGETIC_METAL_MEM_TLSF_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*pm_metal_mem_tlsf_walker_fn)(void *ptr, size_t size, int32_t used, void *user);

size_t pm_metal_mem_tlsf_size(void);
size_t pm_metal_mem_tlsf_pool_overhead(void);
size_t pm_metal_mem_tlsf_align_size(void);
size_t pm_metal_mem_tlsf_alloc_overhead(void);
void *pm_metal_mem_tlsf_create_with_pool(uint8_t *mem, size_t bytes);
void *pm_metal_mem_tlsf_get_pool(void *t);
void *pm_metal_mem_tlsf_add_pool(void *t, uint8_t *mem, size_t bytes);
uint8_t *pm_metal_mem_tlsf_malloc(void *t, size_t size);
uint8_t *pm_metal_mem_tlsf_memalign(void *t, size_t align, size_t size);
uint8_t *pm_metal_mem_tlsf_realloc(void *t, uint8_t *ptr, size_t size);
void pm_metal_mem_tlsf_free(void *t, uint8_t *p);
size_t pm_metal_mem_tlsf_block_size(uint8_t *ptr);
size_t pm_metal_mem_tlsf_block_size_min(void);
size_t pm_metal_mem_tlsf_block_size_max(void);
void *pm_metal_mem_tlsf_create(uint8_t *mem);
void pm_metal_mem_tlsf_destroy(void *t);
void pm_metal_mem_tlsf_remove_pool(void *t, void *pool);
void pm_metal_mem_tlsf_walk_pool(void *pool, pm_metal_mem_tlsf_walker_fn walker, void *user);
int32_t pm_metal_mem_tlsf_check(void *t);
int32_t pm_metal_mem_tlsf_check_pool(void *pool);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_MEM_TLSF_H_ */
