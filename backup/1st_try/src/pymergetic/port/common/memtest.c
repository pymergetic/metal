/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "../headers/memtest.h"

#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef EFAULT
#define EFAULT 14
#endif

typedef uintptr_t pm_port_memtest_word_t;

#define PM_PORT_MEMTEST_PATTERN1 ((pm_port_memtest_word_t)0xA55AA55AA55AA55AULL)
#define PM_PORT_MEMTEST_PATTERN2 ((pm_port_memtest_word_t)0x5AA55AA55AA55AA5ULL)

static int pm_port_memtest_range(uint8_t *base, size_t len, pm_port_memtest_word_t pattern,
				 uintptr_t *fail_addr)
{
	pm_port_memtest_word_t *words = (pm_port_memtest_word_t *)base;
	const size_t word_count = len / sizeof(pm_port_memtest_word_t);
	size_t i;

	for (i = 0U; i < word_count; i++) {
		words[i] = pattern ^ (pm_port_memtest_word_t)i;
	}

	for (i = 0U; i < word_count; i++) {
		const pm_port_memtest_word_t expected = pattern ^ (pm_port_memtest_word_t)i;

		if (words[i] != expected) {
			if (fail_addr != NULL) {
				*fail_addr = (uintptr_t)&words[i];
			}
			return -EFAULT;
		}
	}

	for (i = word_count * sizeof(pm_port_memtest_word_t); i < len; i++) {
		const uint8_t tail_pat = (uint8_t)(pattern ^ (pm_port_memtest_word_t)i);

		base[i] = tail_pat;
	}

	for (i = word_count * sizeof(pm_port_memtest_word_t); i < len; i++) {
		const uint8_t tail_pat = (uint8_t)(pattern ^ (pm_port_memtest_word_t)i);

		if (base[i] != tail_pat) {
			if (fail_addr != NULL) {
				*fail_addr = (uintptr_t)&base[i];
			}
			return -EFAULT;
		}
	}

	return 0;
}

int pm_port_memtest_ex(uint8_t *base, size_t len, uintptr_t *fail_addr)
{
	int rc;

	if (base == NULL || len == 0U) {
		return -EINVAL;
	}

	rc = pm_port_memtest_range(base, len, PM_PORT_MEMTEST_PATTERN1, fail_addr);
	if (rc != 0) {
		return rc;
	}

	return pm_port_memtest_range(base, len, PM_PORT_MEMTEST_PATTERN2, fail_addr);
}
