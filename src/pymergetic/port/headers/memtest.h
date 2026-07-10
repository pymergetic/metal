/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Port-common backing memory test. Pattern write/verify is OS-agnostic;
 * parallel execution is provided per OS (zephyr/memtest_parallel.c, etc.).
 */

#ifndef PM_PORT_MEMTEST_H_
#define PM_PORT_MEMTEST_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Run both test patterns over [base, base + len).
 * Returns 0 on success, or a negative errno (e.g. -EFAULT).
 * When fail_addr is non-NULL, records the first failing address.
 */
int pm_port_memtest_ex(uint8_t *base, size_t len, uintptr_t *fail_addr);

static inline int pm_port_memtest(uint8_t *base, size_t len)
{
	return pm_port_memtest_ex(base, len, NULL);
}

/*
 * Parallel memtest — implemented per OS in port/<os>/memtest_parallel.c.
 * Falls back to pm_port_memtest_ex when only one worker is used.
 */
int pm_port_memtest_parallel(uint8_t *base, size_t len);

#endif /* PM_PORT_MEMTEST_H_ */
