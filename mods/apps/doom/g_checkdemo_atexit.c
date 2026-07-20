/*
 * void(void) trampoline for I_AtExit(G_CheckDemoStatus).
 * WASM rejects casting a returning function to atexit_func_t.
 */
#include "../../../external/doomgeneric/doomgeneric/doomtype.h"

boolean G_CheckDemoStatus(void);

void
G_CheckDemoStatusAtExit(void)
{
	(void)G_CheckDemoStatus();
}
