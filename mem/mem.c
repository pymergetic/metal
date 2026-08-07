#include "pymergetic/metal/mem.h"

#include "tlsf.h"

#include <stdint.h>
#include <string.h>

static tlsf_t g_tlsf;
static uint8_t *g_base;
static size_t g_bytes;
static size_t g_free_est;

int32_t pm_metal_mem_init(uint8_t *base, size_t bytes)
{
    if (base == NULL || bytes < tlsf_size() + tlsf_pool_overhead() + 64u) {
        return -1;
    }
    g_tlsf = tlsf_create_with_pool(base, bytes);
    if (g_tlsf == NULL) {
        return -1;
    }
    g_base = base;
    g_bytes = bytes;
    g_free_est = bytes;
    return 0;
}

uint8_t *pm_metal_mem_alloc(size_t size)
{
    void *p;
    if (g_tlsf == NULL || size == 0) {
        return NULL;
    }
    p = tlsf_malloc(g_tlsf, size);
    return (uint8_t *)p;
}

void pm_metal_mem_free(uint8_t *ptr)
{
    if (g_tlsf == NULL || ptr == NULL) {
        return;
    }
    tlsf_free(g_tlsf, ptr);
}

uint8_t *pm_metal_mem_memalign(size_t align, size_t size)
{
    void *p;
    if (g_tlsf == NULL || size == 0) {
        return NULL;
    }
    if (align < sizeof(void *)) {
        align = sizeof(void *);
    }
    p = tlsf_memalign(g_tlsf, align, size);
    return (uint8_t *)p;
}

uint8_t *pm_metal_mem_realloc(uint8_t *ptr, size_t size)
{
    void *p;
    if (g_tlsf == NULL) {
        return NULL;
    }
    if (size == 0) {
        pm_metal_mem_free(ptr);
        return NULL;
    }
    p = tlsf_realloc(g_tlsf, ptr, size);
    return (uint8_t *)p;
}

size_t pm_metal_mem_heap_bytes(void)
{
    return g_bytes;
}

size_t pm_metal_mem_free_bytes(void)
{
    /* TLSF has no cheap free-bytes; keep estimate for F7 pane later. */
    (void)g_base;
    return g_free_est;
}
