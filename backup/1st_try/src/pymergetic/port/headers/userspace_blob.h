/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Userspace blob: one contiguous RAM window backed by TLSF. libc malloc and
 * mmap are sibling consumers of the same pool.
 */

#ifndef PM_PORT_USERSPACE_BLOB_H_
#define PM_PORT_USERSPACE_BLOB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tlsf.h>

typedef struct pm_userspace_blob_stats {
	size_t total;
	size_t tlsf_used;
	size_t tlsf_free;
	size_t malloc_used;
	size_t mmap_used;
} pm_userspace_blob_stats_t;

int pm_userspace_blob_init(void);
void pm_userspace_blob_ensure(void);
bool pm_userspace_blob_is_ready(void);

tlsf_t pm_userspace_blob_tlsf(void);
uintptr_t pm_userspace_blob_base(void);
size_t pm_userspace_blob_total(void);

int pm_userspace_blob_stats(pm_userspace_blob_stats_t *out);

void pm_userspace_blob_account_malloc_alloc(void *ptr);
void pm_userspace_blob_account_malloc_free(void *ptr);
void pm_userspace_blob_account_malloc_resize(void *old_ptr, void *new_ptr);
void pm_userspace_blob_account_mmap_alloc(void *ptr);
void pm_userspace_blob_account_mmap_free(void *ptr);

bool pm_userspace_blob_memtest_passed(void);

#endif /* PM_PORT_USERSPACE_BLOB_H_ */
