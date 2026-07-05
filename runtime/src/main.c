/*
 * Copyright (c) 2020 BayLibre, SAS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <stdio.h>

int main(void)
{
	printf("Hello from pymergetic-metal on %s\n", CONFIG_BOARD_TARGET);
	printf("  pymergetic/metal: ok\n");
	return 0;
}
