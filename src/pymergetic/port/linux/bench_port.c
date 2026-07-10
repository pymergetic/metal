/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../headers/bench.h"

#include <time.h>

void pm_port_bench_init(void)
{
}

uint64_t pm_port_bench_now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
