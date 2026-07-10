/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * POSIX mmap/munmap routed through the userspace TLSF pool.
 */

#include "../headers/userspace_blob.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#if defined(CONFIG_MMU_PAGE_SIZE)
#define PM_BLOB_PAGE_SIZE CONFIG_MMU_PAGE_SIZE
#else
#define PM_BLOB_PAGE_SIZE 4096
#endif

static size_t pm_blob_round_up(size_t value, size_t align)
{
	const size_t mask = align - 1U;

	return (value + mask) & ~mask;
}

static bool pm_blob_is_anonymous_map(int flags, int fd)
{
	if ((flags & MAP_ANONYMOUS) != 0) {
		return true;
	}

#if defined(MAP_ANON)
	if ((flags & MAP_ANON) != 0) {
		return true;
	}
#endif

	return fd == -1;
}

void *__wrap_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	tlsf_t tlsf;
	void *ptr;
	size_t rounded;

	(void)addr;
	(void)prot;

	if (length == 0U) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (length > pm_userspace_blob_total()) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	if (offset != 0) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	if (!pm_blob_is_anonymous_map(flags, fd)) {
		errno = ENOTSUP;
		return MAP_FAILED;
	}

	pm_userspace_blob_ensure();
	tlsf = pm_userspace_blob_tlsf();
	if (tlsf == NULL) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	rounded = pm_blob_round_up(length, PM_BLOB_PAGE_SIZE);
	if (rounded == 0U) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	ptr = tlsf_memalign(tlsf, PM_BLOB_PAGE_SIZE, rounded);
	if (ptr == NULL) {
		errno = ENOMEM;
		return MAP_FAILED;
	}

	if ((prot & PROT_READ) != 0 || (prot & PROT_WRITE) != 0) {
		memset(ptr, 0, rounded);
	}

	pm_userspace_blob_account_mmap_alloc(ptr);
	return ptr;
}

int __wrap_munmap(void *addr, size_t length)
{
	tlsf_t tlsf;

	(void)length;

	if (addr == NULL || length == 0U) {
		errno = EINVAL;
		return -1;
	}

	pm_userspace_blob_ensure();
	tlsf = pm_userspace_blob_tlsf();
	if (tlsf == NULL) {
		errno = EINVAL;
		return -1;
	}

	pm_userspace_blob_account_mmap_free(addr);
	tlsf_free(tlsf, addr);
	return 0;
}
