/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * libc malloc/free routed through the userspace TLSF pool.
 * Before the blob is ready, fall back to k_malloc (GNU ctor / early boot).
 */

#include "../headers/userspace_blob.h"

#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>

static bool pm_malloc_ptr_in_blob(void *ptr)
{
	uintptr_t addr;
	uintptr_t base;
	size_t total;

	if (ptr == NULL || !pm_userspace_blob_is_ready()) {
		return false;
	}

	addr = (uintptr_t)ptr;
	base = pm_userspace_blob_base();
	total = pm_userspace_blob_total();

	return addr >= base && addr < (base + total);
}

void *malloc(size_t size)
{
	void *ptr;

	if (!pm_userspace_blob_is_ready()) {
		return k_malloc(size);
	}

	ptr = tlsf_malloc(pm_userspace_blob_tlsf(), size);
	if (ptr != NULL) {
		pm_userspace_blob_account_malloc_alloc(ptr);
	}

	return ptr;
}

void free(void *ptr)
{
	if (ptr == NULL) {
		return;
	}

	if (!pm_malloc_ptr_in_blob(ptr)) {
		k_free(ptr);
		return;
	}

	pm_userspace_blob_account_malloc_free(ptr);
	tlsf_free(pm_userspace_blob_tlsf(), ptr);
}

void *calloc(size_t nmemb, size_t size)
{
	size_t total;
	void *ptr;

	if (nmemb != 0U && size > (SIZE_MAX / nmemb)) {
		return NULL;
	}

	total = nmemb * size;
	ptr = malloc(total);
	if (ptr != NULL && total > 0U) {
		memset(ptr, 0, total);
	}

	return ptr;
}

void *realloc(void *ptr, size_t size)
{
	void *new_ptr;

	if (ptr != NULL && !pm_malloc_ptr_in_blob(ptr)) {
		return k_realloc(ptr, size);
	}

	if (!pm_userspace_blob_is_ready()) {
		return k_realloc(ptr, size);
	}

	new_ptr = tlsf_realloc(pm_userspace_blob_tlsf(), ptr, size);
	if (new_ptr != NULL || size == 0U) {
		pm_userspace_blob_account_malloc_resize(ptr, new_ptr);
	}

	return new_ptr;
}
