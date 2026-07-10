/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Host glibc baseline for pm_metal_memory_bench_run workloads.
 */

#include <pymergetic/metal/memory/bench.h>
#include <pymergetic/pm_vis.h>

#include <bench.h>

#include <stdio.h>

int main(void)
{
	printf("pymergetic-metal linux bench\n");
	printf("--------------------------------\n");
	pm_metal_memory_bench_run(pm_port_bench_now_ns, "linux host (glibc)");
	printf("\n");
	return 0;
}
