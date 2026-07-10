/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../headers/userspace_blob.h"
#include "../headers/memtest.h"
#include "../../platform/plat.h"
#include "traits.h"

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_MMU) && !PM_METAL_PORT_IS_FAKE_METAL
#include <zephyr/kernel/mm.h>
#endif

extern char _end[];

#if defined(CONFIG_MMU_PAGE_SIZE)
#define PM_BLOB_PAGE_SIZE CONFIG_MMU_PAGE_SIZE
#else
#define PM_BLOB_PAGE_SIZE 4096
#endif

#if PM_METAL_PORT_IS_FAKE_METAL && defined(CONFIG_PM_USERSPACE_BLOB_SIZE) && \
	(CONFIG_PM_USERSPACE_BLOB_SIZE > 0)

static unsigned char pm_userspace_blob_buf[CONFIG_PM_USERSPACE_BLOB_SIZE]
	__aligned(PM_BLOB_PAGE_SIZE);

#endif

#if defined(CONFIG_MMU) && !PM_METAL_PORT_IS_FAKE_METAL
static void *pm_userspace_blob_mapped;
static size_t pm_userspace_blob_mapped_size;
#endif

static tlsf_t pm_userspace_tlsf;
static uintptr_t pm_userspace_blob_region_base;
static size_t pm_userspace_blob_region_size;
static bool pm_userspace_blob_ready;
static size_t pm_userspace_malloc_used;
static size_t pm_userspace_mmap_used;
#if defined(CONFIG_PM_USERSPACE_BLOB_MEMTEST)
static bool pm_userspace_blob_memtest_ok;
#endif

struct pm_userspace_blob_walk {
	size_t used;
	size_t free;
};

static void pm_userspace_blob_walker(void *ptr, size_t size, int used, void *user)
{
	struct pm_userspace_blob_walk *acc = user;

	(void)ptr;
	if (used) {
		acc->used += size;
	} else {
		acc->free += size;
	}
}

static size_t pm_userspace_blob_budget(void)
{
	return pm_plat_arena_budget();
}

static int pm_userspace_blob_establish_backing(void)
{
#if PM_METAL_PORT_IS_FAKE_METAL && defined(CONFIG_PM_USERSPACE_BLOB_SIZE) && \
	(CONFIG_PM_USERSPACE_BLOB_SIZE > 0)

	pm_userspace_blob_region_base = (uintptr_t)pm_userspace_blob_buf;
	pm_userspace_blob_region_size = (size_t)CONFIG_PM_USERSPACE_BLOB_SIZE;
	return 0;

#elif defined(CONFIG_MMU) && !PM_METAL_PORT_IS_FAKE_METAL

	const uintptr_t link_end = ROUND_UP((uintptr_t)&_end, 8);
	size_t blob_size = ROUND_DOWN(pm_userspace_blob_budget(), PM_BLOB_PAGE_SIZE);

	if (blob_size == 0U) {
		return -EINVAL;
	}

	if (pm_userspace_blob_mapped == NULL) {
		uintptr_t phys;
		void *virt = NULL;

		phys = (uintptr_t)k_mem_phys_addr((void *)link_end);
		k_mem_map_phys_bare((uint8_t **)&virt, phys, blob_size, K_MEM_PERM_RW);
		if (virt == NULL) {
			return -ENOMEM;
		}
		pm_userspace_blob_mapped = virt;
		pm_userspace_blob_mapped_size = blob_size;
	}

	pm_userspace_blob_region_base = (uintptr_t)pm_userspace_blob_mapped;
	pm_userspace_blob_region_size = pm_userspace_blob_mapped_size;
	return 0;

#else

	const uintptr_t link_end = ROUND_UP((uintptr_t)&_end, 8);
	uintptr_t blob_base = ROUND_UP(link_end, PM_BLOB_PAGE_SIZE);
	size_t blob_size = ROUND_DOWN(pm_userspace_blob_budget(), PM_BLOB_PAGE_SIZE);

	if (blob_size == 0U) {
		return -EINVAL;
	}

	pm_userspace_blob_region_base = blob_base;
	pm_userspace_blob_region_size = blob_size;
	return 0;

#endif
}

static int pm_userspace_blob_boot_init(void)
{
	if (pm_userspace_blob_ready) {
		return 0;
	}

	if (pm_userspace_blob_establish_backing() != 0) {
		return -EINVAL;
	}

	if (pm_userspace_blob_region_size <= tlsf_size() + tlsf_pool_overhead()) {
		return -EINVAL;
	}

#if defined(CONFIG_PM_USERSPACE_BLOB_MEMTEST)
	if (pm_port_memtest_parallel((uint8_t *)pm_userspace_blob_region_base,
				     pm_userspace_blob_region_size) != 0) {
		pm_userspace_blob_memtest_ok = false;
		return -EFAULT;
	}
	pm_userspace_blob_memtest_ok = true;
#endif

	pm_userspace_tlsf = tlsf_create_with_pool((void *)pm_userspace_blob_region_base,
						  pm_userspace_blob_region_size);
	if (pm_userspace_tlsf == NULL) {
		return -ENOMEM;
	}

	pm_userspace_blob_ready = true;
	return 0;
}

int pm_userspace_blob_init(void)
{
	return pm_userspace_blob_boot_init();
}

void pm_userspace_blob_ensure(void)
{
	/* Blob is established explicitly from boot; avoid early init via libc ctors. */
}

bool pm_userspace_blob_is_ready(void)
{
	return pm_userspace_blob_ready;
}

tlsf_t pm_userspace_blob_tlsf(void)
{
	return pm_userspace_tlsf;
}

uintptr_t pm_userspace_blob_base(void)
{
	return pm_userspace_blob_region_base;
}

size_t pm_userspace_blob_total(void)
{
	return pm_userspace_blob_region_size;
}

static void pm_userspace_blob_account_add(size_t *counter, void *ptr)
{
	if (ptr != NULL) {
		*counter += tlsf_block_size(ptr);
	}
}

static void pm_userspace_blob_account_sub(size_t *counter, void *ptr)
{
	size_t block_size;

	if (ptr == NULL) {
		return;
	}

	block_size = tlsf_block_size(ptr);
	if (*counter >= block_size) {
		*counter -= block_size;
	} else {
		*counter = 0U;
	}
}

void pm_userspace_blob_account_malloc_alloc(void *ptr)
{
	pm_userspace_blob_account_add(&pm_userspace_malloc_used, ptr);
}

void pm_userspace_blob_account_malloc_free(void *ptr)
{
	pm_userspace_blob_account_sub(&pm_userspace_malloc_used, ptr);
}

void pm_userspace_blob_account_malloc_resize(void *old_ptr, void *new_ptr)
{
	pm_userspace_blob_account_sub(&pm_userspace_malloc_used, old_ptr);
	pm_userspace_blob_account_add(&pm_userspace_malloc_used, new_ptr);
}

void pm_userspace_blob_account_mmap_alloc(void *ptr)
{
	pm_userspace_blob_account_add(&pm_userspace_mmap_used, ptr);
}

void pm_userspace_blob_account_mmap_free(void *ptr)
{
	pm_userspace_blob_account_sub(&pm_userspace_mmap_used, ptr);
}

int pm_userspace_blob_stats(pm_userspace_blob_stats_t *out)
{
	struct pm_userspace_blob_walk walk = {0};

	if (out == NULL) {
		return -EINVAL;
	}
	if (!pm_userspace_blob_ready && pm_userspace_blob_boot_init() != 0) {
		return -EINVAL;
	}

	tlsf_walk_pool(tlsf_get_pool(pm_userspace_tlsf), pm_userspace_blob_walker, &walk);
	out->total = pm_userspace_blob_region_size;
	out->tlsf_used = walk.used;
	out->tlsf_free = walk.free;
	out->malloc_used = pm_userspace_malloc_used;
	out->mmap_used = pm_userspace_mmap_used;
	return 0;
}

#if defined(CONFIG_PM_USERSPACE_BLOB_MEMTEST)
bool pm_userspace_blob_memtest_passed(void)
{
	return pm_userspace_blob_memtest_ok;
}
#else
bool pm_userspace_blob_memtest_passed(void)
{
	return true;
}
#endif
