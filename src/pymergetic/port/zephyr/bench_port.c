/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../headers/bench.h"

#include <zephyr/kernel.h>

#if defined(CONFIG_BOARD_NATIVE_SIM)

static uint64_t pm_bench_tsc_per_ns;

static uint64_t pm_bench_rdtsc(void)
{
	uint32_t lo;
	uint32_t hi;

	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return ((uint64_t)hi << 32) | (uint64_t)lo;
}

void pm_port_bench_init(void)
{
	const uint64_t t0 = pm_bench_rdtsc();

	k_msleep(50);
	pm_bench_tsc_per_ns = (pm_bench_rdtsc() - t0) / 50000000ULL;
	if (pm_bench_tsc_per_ns == 0U) {
		pm_bench_tsc_per_ns = 1U;
	}
}

uint64_t pm_port_bench_now_ns(void)
{
	return pm_bench_rdtsc() / pm_bench_tsc_per_ns;
}

#else

void pm_port_bench_init(void)
{
}

uint64_t pm_port_bench_now_ns(void)
{
	return k_cyc_to_ns_ceil64(k_cycle_get_64());
}

#endif
