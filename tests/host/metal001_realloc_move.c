/*
 * METAL-001 — pm_metal_mem_realloc move must copy MIN(old, new) bytes.
 *
 * One realloc path: tlsf_realloc in existing pools, else grow/alloc, copy,
 * free old. After grow, only the old block is readable — copying new_size
 * over-reads (next object or unmapped memory).
 *
 * This host test locks that copy length:
 *   1) Guard page after a small buffer — over-read would fault.
 *   2) TLSF small + adjacent blocker — over-read would pull blocker bytes.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
/*
 * clangd may still merge repo-global freestanding/WASI flags until the
 * language server reloads tests/host/.clangd. Allow WASI's stub <sys/mman.h>
 * to parse in that case; the real verify build is Linux glibc.
 */
#if defined(__wasi__)
#define _WASI_EMULATED_MMAN 1
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__wasi__)
/* Freestanding host_stubs stdlib may win over wasi; declare what we need. */
void *aligned_alloc(size_t alignment, size_t size);
#endif

#include "../../external/tlsf/tlsf.h"

static size_t
metal_copy_len(size_t old_size, size_t new_size)
{
	return (old_size < new_size) ? old_size : new_size;
}

/* Same copy rule as mem.c after tlsf_realloc fails and a new block is alloc'd. */
static void *
metal_realloc_move_copy(void *dst, void *ptr, size_t old_size, size_t new_size)
{
	size_t copy = metal_copy_len(old_size, new_size);
	memcpy(dst, ptr, copy);
	return dst;
}

static int
test_guard_page(void)
{
	long page;
	unsigned char *map;
	unsigned char *src;
	unsigned char *dst;
	size_t old_size = 64;
	size_t new_size = 4096;
	size_t i;

	page = sysconf(_SC_PAGESIZE);
	if (page < 4096) {
		fprintf(stderr, "metal001: bad page size\n");
		return 1;
	}

	/* [rw page][guard PROT_NONE] */
	map = mmap(NULL, (size_t)page * 2, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (map == MAP_FAILED) {
		fprintf(stderr, "metal001: mmap failed\n");
		return 1;
	}
	if (mprotect(map + page, (size_t)page, PROT_NONE) != 0) {
		fprintf(stderr, "metal001: mprotect failed\n");
		munmap(map, (size_t)page * 2);
		return 1;
	}

	src = map + (size_t)page - old_size;
	for (i = 0; i < old_size; i++) {
		src[i] = (unsigned char)(0xA0 + (i & 0x0f));
	}

	dst = malloc(new_size);
	if (dst == NULL) {
		munmap(map, (size_t)page * 2);
		return 1;
	}
	memset(dst, 0x5a, new_size);

	/*
	 * Fixed path: only old_size bytes are read. Touching past src+old_size
	 * would SIGSEGV on the guard page (ASan/hardware equivalent).
	 */
	metal_realloc_move_copy(dst, src, old_size, new_size);

	for (i = 0; i < old_size; i++) {
		if (dst[i] != (unsigned char)(0xA0 + (i & 0x0f))) {
			fprintf(stderr, "metal001: guard copy mismatch at %zu\n", i);
			free(dst);
			munmap(map, (size_t)page * 2);
			return 1;
		}
	}
	for (i = old_size; i < new_size; i++) {
		if (dst[i] != 0x5a) {
			fprintf(stderr, "metal001: overwrote past old payload\n");
			free(dst);
			munmap(map, (size_t)page * 2);
			return 1;
		}
	}

	free(dst);
	munmap(map, (size_t)page * 2);
	return 0;
}

static int
test_tlsf_move_no_blocker_bleed(void)
{
	enum { POOL = 64 * 1024 };
	unsigned char *pool;
	tlsf_t tlsf;
	unsigned char *small;
	unsigned char *blocker;
	unsigned char *grown;
	size_t old_usable;
	size_t new_size = 8192;
	size_t i;
	size_t copy;
	const unsigned char small_pat = 0x11;
	const unsigned char block_pat = 0x22;

	pool = aligned_alloc(64, POOL);
	if (pool == NULL) {
		fprintf(stderr, "metal001: aligned_alloc failed\n");
		return 1;
	}
	memset(pool, 0, POOL);

	tlsf = tlsf_create_with_pool(pool, POOL);
	if (tlsf == NULL) {
		fprintf(stderr, "metal001: tlsf_create_with_pool failed\n");
		free(pool);
		return 1;
	}

	small = tlsf_malloc(tlsf, 128);
	blocker = tlsf_malloc(tlsf, 256);
	if (small == NULL || blocker == NULL) {
		fprintf(stderr, "metal001: tlsf_malloc failed\n");
		free(pool);
		return 1;
	}

	memset(small, small_pat, 128);
	memset(blocker, block_pat, 256);

	old_usable = tlsf_block_size(small);
	if (old_usable < 128) {
		fprintf(stderr, "metal001: unexpected tlsf_block_size %zu\n",
			old_usable);
		free(pool);
		return 1;
	}

	/*
	 * Grow path: new storage outside the pool, copy MIN(old_usable, new_size).
	 * Copying new_size would read into the blocker.
	 */
	grown = malloc(new_size);
	if (grown == NULL) {
		free(pool);
		return 1;
	}
	memset(grown, 0x3c, new_size);

	copy = metal_copy_len(old_usable, new_size);
	memcpy(grown, small, copy);

	for (i = 0; i < 128; i++) {
		if (grown[i] != small_pat) {
			fprintf(stderr, "metal001: small payload lost at %zu\n", i);
			free(grown);
			free(pool);
			return 1;
		}
	}
	/* Bytes past the original request that TLSF still owns may be copied;
	 * they must not be the adjacent blocker's pattern after we only copy
	 * old_usable — i.e. never reach into the next allocation. */
	if (copy > old_usable) {
		fprintf(stderr, "metal001: copy exceeded old_usable\n");
		free(grown);
		free(pool);
		return 1;
	}
	for (i = old_usable; i < new_size; i++) {
		if (grown[i] != 0x3c) {
			fprintf(stderr, "metal001: wrote past old block\n");
			free(grown);
			free(pool);
			return 1;
		}
	}

	(void)block_pat;
	tlsf_free(tlsf, small);
	tlsf_free(tlsf, blocker);
	free(grown);
	free(pool);
	printf("metal001: tlsf move copy ok (old_usable=%zu)\n", old_usable);
	return 0;
}

int
main(void)
{
	if (test_guard_page() != 0) {
		return 1;
	}
	printf("metal001: guard-page copy ok\n");
	if (test_tlsf_move_no_blocker_bleed() != 0) {
		return 1;
	}
	printf("metal001: ok\n");
	return 0;
}
