/*
 * T0 — hello via Metal shell_log (no WASI stdio).
 */
#include "pymergetic/metal/shell/shell/shell.h"

int
main(void)
{
	pm_metal_shell_log("t0_hello");
	return 0;
}
