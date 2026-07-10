/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <pymergetic/metal/memory/bench.h>

#if defined(__ZEPHYR__)
#include "../../port/headers/bench.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__ZEPHYR__) && defined(CONFIG_PM_USERSPACE_BLOB)
#include <sys/mman.h>
#define PM_BENCH_HAS_MMAP 1
#elif !defined(__ZEPHYR__)
#include <sys/mman.h>
#define PM_BENCH_HAS_MMAP 1
#else
#define PM_BENCH_HAS_MMAP 0
#endif

#define PM_BENCH_MALLOC_SMALL_ITERS  10000U
#define PM_BENCH_MALLOC_SMALL_SIZE   64U
#define PM_BENCH_MALLOC_MIXED_ROUNDS 1000U
#define PM_BENCH_MMAP_ITERS          1000U
#define PM_BENCH_MMAP_SIZE           4096U

static uint64_t pm_bench_elapsed_ns(pm_metal_bench_now_ns_fn now_ns, uint64_t start_ns)
{
	return now_ns() - start_ns;
}

static void pm_bench_print_line(const char *label, uint64_t elapsed_ns, uint64_t ops)
{
	const double elapsed_ms = (double)elapsed_ns / 1000000.0;
	double ops_per_s = 0.0;

	if (elapsed_ns > 0U) {
		ops_per_s = (double)ops / ((double)elapsed_ns / 1000000000.0);
	}

	if (ops_per_s >= 1000000.0) {
		if (elapsed_ms < 1.0) {
			printf("    %-22s %8.2f us  (%.2f Mops/s)\n", label,
			       (double)elapsed_ns / 1000.0, ops_per_s / 1000000.0);
		} else {
			printf("    %-22s %8.2f ms  (%.2f Mops/s)\n", label, elapsed_ms,
			       ops_per_s / 1000000.0);
		}
	} else if (ops_per_s >= 1000.0) {
		printf("    %-22s %8.2f ms  (%.2f Kops/s)\n", label, elapsed_ms, ops_per_s / 1000.0);
	} else {
		printf("    %-22s %8.2f ms  (%.0f ops/s)\n", label, elapsed_ms, ops_per_s);
	}
}

static void pm_bench_malloc_small(pm_metal_bench_now_ns_fn now_ns)
{
	uint64_t start_ns = now_ns();
	uint32_t i;

	for (i = 0U; i < PM_BENCH_MALLOC_SMALL_ITERS; i++) {
		void *ptr = malloc(PM_BENCH_MALLOC_SMALL_SIZE);

		if (ptr == NULL) {
			printf("    %-22s failed (malloc)\n", "malloc 64B");
			return;
		}

		*(volatile char *)ptr = (char)i;
		free(ptr);
	}

	pm_bench_print_line("malloc 64B", pm_bench_elapsed_ns(now_ns, start_ns),
			    PM_BENCH_MALLOC_SMALL_ITERS);
}

static void pm_bench_malloc_mixed(pm_metal_bench_now_ns_fn now_ns)
{
	static const size_t sizes[] = {32U, 128U, 512U, 4096U};
	void *ptrs[4];
	uint64_t start_ns = now_ns();
	uint32_t round;
	size_t i;

	for (round = 0U; round < PM_BENCH_MALLOC_MIXED_ROUNDS; round++) {
		for (i = 0U; i < 4U; i++) {
			ptrs[i] = malloc(sizes[i]);
			if (ptrs[i] == NULL) {
				printf("    %-22s failed (malloc)\n", "malloc mixed");
				return;
			}
		}

		for (i = 0U; i < 4U; i++) {
			free(ptrs[i]);
		}
	}

	pm_bench_print_line("malloc mixed", pm_bench_elapsed_ns(now_ns, start_ns),
			    (uint64_t)PM_BENCH_MALLOC_MIXED_ROUNDS * 4U);
}

static void pm_bench_realloc_grow(pm_metal_bench_now_ns_fn now_ns)
{
	void *ptr = malloc(64U);
	uint64_t start_ns;
	uint32_t i;

	if (ptr == NULL) {
		printf("    %-22s failed (malloc)\n", "realloc grow");
		return;
	}

	start_ns = now_ns();
	for (i = 0U; i < 256U; i++) {
		ptr = realloc(ptr, 64U + ((size_t)i * 16U));
		if (ptr == NULL) {
			printf("    %-22s failed (realloc)\n", "realloc grow");
			return;
		}
		*(volatile char *)ptr = (char)i;
	}

	free(ptr);
	pm_bench_print_line("realloc grow", pm_bench_elapsed_ns(now_ns, start_ns), 256U);
}

#if PM_BENCH_HAS_MMAP
static void pm_bench_mmap_page(pm_metal_bench_now_ns_fn now_ns)
{
	uint64_t start_ns = now_ns();
	uint32_t i;

	for (i = 0U; i < PM_BENCH_MMAP_ITERS; i++) {
		void *map = mmap(NULL, PM_BENCH_MMAP_SIZE, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

		if (map == MAP_FAILED) {
			printf("    %-22s failed (mmap)\n", "mmap 4KiB");
			return;
		}

		*(volatile char *)map = (char)i;
		if (munmap(map, PM_BENCH_MMAP_SIZE) != 0) {
			printf("    %-22s failed (munmap)\n", "mmap 4KiB");
			return;
		}
	}

	pm_bench_print_line("mmap 4KiB", pm_bench_elapsed_ns(now_ns, start_ns), PM_BENCH_MMAP_ITERS);
}
#endif

void pm_metal_memory_bench_run(pm_metal_bench_now_ns_fn now_ns, const char *platform_label)
{
	if (now_ns == NULL) {
		return;
	}

	printf("\n-> Userspace allocator bench\n");
	if (platform_label != NULL && platform_label[0] != '\0') {
		printf("    platform: %s\n", platform_label);
	}

#if defined(__ZEPHYR__)
	pm_port_bench_init();
#endif

	pm_bench_malloc_small(now_ns);
	pm_bench_malloc_mixed(now_ns);
	pm_bench_realloc_grow(now_ns);
#if PM_BENCH_HAS_MMAP
	pm_bench_mmap_page(now_ns);
#endif

#if defined(__ZEPHYR__)
	printf("    compare: scripts/run-linux-bench.sh (host glibc)\n");
#if defined(CONFIG_BOARD_NATIVE_SIM)
	printf("    timer: host TSC/RDTSC (native_sim — real host CPU)\n");
#else
	printf("    timer: k_cycle_get_64 (QEMU/HW — TCG emulation skew on QEMU)\n");
#endif
#endif
}
