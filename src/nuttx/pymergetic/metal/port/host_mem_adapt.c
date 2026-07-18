/*
 * Host mmap pools for NuttX sim Metal memory/{kheap,bytecode}. Final nuttx
 * link only (not nuttx.rel) — same shape as host_net_adapt.c. NuttX's own
 * SIM_HEAP_SIZE (64 MiB) cannot cover python.wasm's server-class pools.
 */
#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>

void *pm_metal_host_pool_alloc(size_t bytes)
{
	void *p;

	if (bytes == 0) {
		return NULL;
	}
	p = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		return NULL;
	}
	return p;
}

void pm_metal_host_pool_free(void *ptr, size_t bytes)
{
	if (ptr && bytes) {
		(void)munmap(ptr, bytes);
	}
}
