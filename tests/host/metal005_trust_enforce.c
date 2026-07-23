/*
 * METAL-005 — enforce mode failures are intrinsically fatal.
 * Escape hatch ENFORCE_CONTINUE is off by default.
 */
#include <stdio.h>

#define MODE_OFF     0
#define MODE_SOFT    1
#define MODE_ENFORCE 2

#ifndef ENFORCE_CONTINUE
#define ENFORCE_CONTINUE 0
#endif

static int
boot_return_fail(int mode, int soft_strict)
{
	if (mode == MODE_ENFORCE) {
#if ENFORCE_CONTINUE
		return 0;
#else
		(void)soft_strict;
		return -1;
#endif
	}
	return soft_strict ? -1 : 0;
}

int
main(void)
{
	if (boot_return_fail(MODE_ENFORCE, 0) != -1) {
		fprintf(stderr, "metal005: enforce must be fatal\n");
		return 1;
	}
	if (boot_return_fail(MODE_SOFT, 0) != 0) {
		fprintf(stderr, "metal005: soft must continue\n");
		return 1;
	}
	if (boot_return_fail(MODE_SOFT, 1) != -1) {
		fprintf(stderr, "metal005: soft+strict must fail\n");
		return 1;
	}
	printf("metal005: ok\n");
	return 0;
}
