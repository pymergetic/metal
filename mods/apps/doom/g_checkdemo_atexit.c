/*
 * Keep external/doomgeneric vanilla.
 *
 * Vanilla does: I_AtExit((atexit_func_t)G_CheckDemoStatus, true);
 * WASM call_indirect rejects that cast (boolean vs void). Intercept via
 * --wrap=I_AtExit and register a void(void) trampoline instead.
 */
#include "../../../external/doomgeneric/doomgeneric/doomtype.h"
#include "../../../external/doomgeneric/doomgeneric/i_system.h"

boolean G_CheckDemoStatus(void);

void __real_I_AtExit(atexit_func_t func, boolean run_on_error);

static void
G_CheckDemoStatusAtExit(void)
{
	(void)G_CheckDemoStatus();
}

void
__wrap_I_AtExit(atexit_func_t func, boolean run_on_error)
{
	if (func == (atexit_func_t)G_CheckDemoStatus) {
		func = G_CheckDemoStatusAtExit;
	}

	__real_I_AtExit(func, run_on_error);
}
