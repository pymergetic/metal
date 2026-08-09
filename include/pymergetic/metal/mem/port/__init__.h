#ifndef PYMERGETIC_METAL_MEM_PORT_H_
#define PYMERGETIC_METAL_MEM_PORT_H_
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int32_t pm_metal_mem_init(void *base, size_t bytes);
void *pm_metal_mem_alloc(size_t bytes);
void pm_metal_mem_free(void *ptr);
size_t pm_metal_mem_free_bytes(void);
#ifdef __cplusplus
}
#endif
#endif
