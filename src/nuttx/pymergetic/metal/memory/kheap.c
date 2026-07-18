/*
 * Memory — nuttx kheap ops (bind).
 *
 * On sim, large pools come from host mmap (host_mem_adapt.c) because the
 * NuttX SIM_HEAP_SIZE is only 64 MiB. Elsewhere, fall back to malloc.
 */
#include "pymergetic/metal/memory/kheap.h"

#include <stddef.h>
#include <stdlib.h>

/* PM_METAL_NUTTX_HOST_POOL is set by CMake on CONFIG_ARCH_SIM (not via
 * nuttx/config.h — that header is outside clangd's Metal compile DB). */
#if defined(PM_METAL_NUTTX_HOST_POOL)
/* Defined in host_mem_adapt.c (final nuttx link). */
void *pm_metal_host_pool_alloc(size_t bytes);
void pm_metal_host_pool_free(void *ptr, size_t bytes);
#endif

static void *g_pm_metal_memory_kheap_pool;
static uint64_t g_pm_metal_memory_kheap_bytes;

static void *pm_metal_memory_nuttx_kheap_establish(uint64_t requested_bytes, uint64_t *out_bytes)
{
	void *pool;

	if (requested_bytes == 0 || requested_bytes > SIZE_MAX) {
		return NULL;
	}

#if defined(PM_METAL_NUTTX_HOST_POOL)
	pool = pm_metal_host_pool_alloc((size_t)requested_bytes);
#else
	pool = malloc((size_t)requested_bytes);
#endif
	if (!pool) {
		return NULL;
	}

	g_pm_metal_memory_kheap_pool = pool;
	g_pm_metal_memory_kheap_bytes = requested_bytes;
	*out_bytes = requested_bytes;

	return pool;
}

static void pm_metal_memory_nuttx_kheap_release(void)
{
#if defined(PM_METAL_NUTTX_HOST_POOL)
	pm_metal_host_pool_free(g_pm_metal_memory_kheap_pool, (size_t)g_pm_metal_memory_kheap_bytes);
#else
	free(g_pm_metal_memory_kheap_pool);
#endif
	g_pm_metal_memory_kheap_pool = NULL;
	g_pm_metal_memory_kheap_bytes = 0;
}

static uint64_t pm_metal_memory_nuttx_kheap_bytes(void)
{
	return g_pm_metal_memory_kheap_bytes;
}

static const pm_metal_memory_ops_t g_pm_metal_memory_kheap_ops = {
	.probe = NULL,
	.establish = pm_metal_memory_nuttx_kheap_establish,
	.release = pm_metal_memory_nuttx_kheap_release,
	.bytes = pm_metal_memory_nuttx_kheap_bytes,
	.alloc = NULL,
	.free = NULL,
};

const pm_metal_memory_ops_t *pm_metal_memory_kheap_ops(void)
{
	return &g_pm_metal_memory_kheap_ops;
}
