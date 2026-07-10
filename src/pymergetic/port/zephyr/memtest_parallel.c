/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../headers/memtest.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pm_port_memtest, LOG_LEVEL_ERR);

#define PM_PORT_MEMTEST_MAX_WORKERS 4U
#define PM_PORT_MEMTEST_MIN_PARALLEL ((size_t)(64 * 1024))
#define PM_PORT_MEMTEST_STACK_SIZE   1024U

K_THREAD_STACK_ARRAY_DEFINE(pm_port_memtest_stacks, PM_PORT_MEMTEST_MAX_WORKERS,
			    PM_PORT_MEMTEST_STACK_SIZE);
static struct k_thread pm_port_memtest_threads[PM_PORT_MEMTEST_MAX_WORKERS];

struct pm_port_memtest_job {
	uint8_t *base;
	size_t len;
	int result;
	uintptr_t fail_addr;
};

static void pm_port_memtest_worker(void *p1, void *p2, void *p3)
{
	struct pm_port_memtest_job *job = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	job->result = pm_port_memtest_ex(job->base, job->len, &job->fail_addr);
}

static unsigned int pm_port_memtest_worker_count(size_t size)
{
#if defined(CONFIG_SMP) && (CONFIG_MP_MAX_NUM_CPUS > 1)
	unsigned int workers = MIN((unsigned int)CONFIG_MP_MAX_NUM_CPUS, PM_PORT_MEMTEST_MAX_WORKERS);

	if (size < PM_PORT_MEMTEST_MIN_PARALLEL * workers) {
		return 1U;
	}

	return workers;
#else
	ARG_UNUSED(size);
	return 1U;
#endif
}

int pm_port_memtest_parallel(uint8_t *base, size_t len)
{
	const unsigned int workers = pm_port_memtest_worker_count(len);
	struct pm_port_memtest_job jobs[PM_PORT_MEMTEST_MAX_WORKERS];
	const size_t chunk = ROUND_DOWN(len / workers, sizeof(uintptr_t));
	uintptr_t fail_addr = 0U;
	unsigned int i;
	int rc;

	if (workers == 1U) {
		rc = pm_port_memtest_ex(base, len, &fail_addr);
		if (rc != 0) {
			LOG_ERR("memtest failed at 0x%lx", (unsigned long)fail_addr);
		}
		return rc;
	}

	for (i = 0U; i < workers; i++) {
		jobs[i].base = base + (i * chunk);
		jobs[i].len = (i == workers - 1U) ? (len - (i * chunk)) : chunk;
		jobs[i].result = 0;
		jobs[i].fail_addr = 0U;

		k_thread_create(&pm_port_memtest_threads[i], pm_port_memtest_stacks[i],
				K_THREAD_STACK_SIZEOF(pm_port_memtest_stacks[i]),
				pm_port_memtest_worker, &jobs[i], NULL, NULL, K_PRIO_COOP(7), 0,
				K_NO_WAIT);
	}

	for (i = 0U; i < workers; i++) {
		k_thread_join(&pm_port_memtest_threads[i], K_FOREVER);
		if (jobs[i].result != 0) {
			LOG_ERR("memtest worker %u failed at 0x%lx", i,
				(unsigned long)jobs[i].fail_addr);
			return jobs[i].result;
		}
	}

	return 0;
}
