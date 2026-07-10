/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Linux port: parallel memtest entry. Sequential for now; swap in pthread
 * workers using the same pm_port_memtest_ex() per-chunk split as Zephyr.
 */

#include "../headers/memtest.h"

int pm_port_memtest_parallel(uint8_t *base, size_t len)
{
	return pm_port_memtest_ex(base, len, NULL);
}
