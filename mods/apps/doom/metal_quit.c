/*
 * --wrap=I_Quit / I_Error — end the wasm session via wasi proc_exit.
 * Host maps that to PM_METAL_DONE / ERROR. Keep external/doomgeneric vanilla.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "metal_doom.h"

/*
 * clangd merges global -I host_stubs (freestanding stdlib without exit).
 * Compatible redeclaration when wasi/host stdlib already declared it.
 */
void exit(int status);

void __real_I_Quit(void);

static int s_quit_req;
static int s_quit_code;

void
metal_doom_request_quit(int code)
{
	s_quit_req  = 1;
	s_quit_code = code;
}

int
metal_doom_quit_requested(void)
{
	return s_quit_req;
}

int
metal_doom_quit_code(void)
{
	return s_quit_code;
}

void
__wrap_I_Quit(void)
{
	__real_I_Quit();
	metal_doom_request_quit(0);
	exit(0);
}

void
__wrap_I_Error(char *error, ...)
{
	va_list argptr;
	char    msgbuf[256];

	va_start(argptr, error);
	vsnprintf(msgbuf, sizeof(msgbuf), error != NULL ? error : "I_Error",
		  argptr);
	va_end(argptr);
	fprintf(stderr, "metal-doom: %s\n", msgbuf);

	metal_doom_request_quit(1);
	/* Skip vanilla zenity/system path — exit cleanly for the host. */
	exit(1);
}
