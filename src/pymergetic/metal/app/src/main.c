/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <pymergetic/metal/hello.h>

int main(void)
{
	printf("Hello from pymergetic-metal on %s\n", CONFIG_BOARD_TARGET);
	pm_metal_hello();
	return 0;
}
