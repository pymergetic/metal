/*
 * --wrap=I_Quit / I_Error — end the wasm session via wasi proc_exit.
 * Host maps that to PM_METAL_DONE / ERROR. Keep external/doomgeneric vanilla.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "../../../external/doomgeneric/doomgeneric/doomstat.h"

#include "metal_doom.h"

extern boolean demoplayback;
extern boolean singledemo;
extern boolean menuactive;
extern int     messageToPrint;

/*
 * clangd merges global -I host_stubs (freestanding stdlib without exit).
 * Compatible redeclaration when wasi/host stdlib already declared it.
 */
void exit(int status);

void __real_I_Quit(void);

static int s_quit_req;
static int s_quit_code;

void metal_doom_request_quit(int code)
{
  s_quit_req  = 1;
  s_quit_code = code;
}

void metal_doom_clear_quit(void)
{
  s_quit_req  = 0;
  s_quit_code = 0;
}

int metal_doom_quit_requested(void)
{
  return s_quit_req;
}

int metal_doom_quit_code(void)
{
  return s_quit_code;
}

void __wrap_I_Quit(void)
{
  /*
	 * Only call sites: M_QuitResponse('y'), G_CheckDemoStatus(singledemo),
	 * testcontrols Esc. Message handler clears messageToPrint before
	 * M_QuitResponse runs — so menu=1 msg=0 is the normal Quit+Y path.
	 */
  fprintf(stderr,
          "metal-doom: I_Quit (gamestate=%d demo=%d singledemo=%d "
          "menu=%d msg=%d)%s\n",
          (int)gamestate,
          (int)demoplayback,
          (int)singledemo,
          (int)menuactive,
          messageToPrint,
          (menuactive && !demoplayback && !singledemo) ? " [menu Quit confirm / M_QuitResponse]"
                                                       : (singledemo ? " [singledemo]" : ""));
  __real_I_Quit();
  metal_doom_request_quit(0);
  exit(0);
}

void __wrap_I_Error(char *error, ...)
{
  va_list argptr;
  char    msgbuf[256];

  va_start(argptr, error);
  vsnprintf(msgbuf, sizeof(msgbuf), error != NULL ? error : "I_Error", argptr);
  va_end(argptr);
  fprintf(stderr, "metal-doom: I_Error: %s\n", msgbuf);

  metal_doom_request_quit(1);
  /* Skip vanilla zenity/system path — exit cleanly for the host. */
  exit(1);
}
