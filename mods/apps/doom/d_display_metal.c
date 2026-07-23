/*
 * --wrap=D_Display / doomgeneric_Tick — cooperative wipe (one melt frame / Tick).
 * Vanilla D_Display nests I_Sleep until melt completes; that hogs one guest_step.
 */

#include "../../../external/doomgeneric/doomgeneric/doomdef.h"
#include "../../../external/doomgeneric/doomgeneric/doomstat.h"
#include "../../../external/doomgeneric/doomgeneric/doomtype.h"
#include "../../../external/doomgeneric/doomgeneric/d_main.h"
#include "../../../external/doomgeneric/doomgeneric/d_loop.h"
#include "../../../external/doomgeneric/doomgeneric/f_finale.h"
#include "../../../external/doomgeneric/doomgeneric/f_wipe.h"
#include "../../../external/doomgeneric/doomgeneric/hu_stuff.h"
#include "../../../external/doomgeneric/doomgeneric/i_video.h"
#include "../../../external/doomgeneric/doomgeneric/m_menu.h"
#include "../../../external/doomgeneric/doomgeneric/am_map.h"
#include "../../../external/doomgeneric/doomgeneric/r_local.h" // IWYU pragma: keep
#include "../../../external/doomgeneric/doomgeneric/st_stuff.h"
#include "../../../external/doomgeneric/doomgeneric/v_video.h"
#include "../../../external/doomgeneric/doomgeneric/wi_stuff.h"
#include "../../../external/doomgeneric/doomgeneric/w_wad.h"
#include "../../../external/doomgeneric/doomgeneric/z_zone.h"
#include "../../../external/doomgeneric/doomgeneric/deh_str.h"

#include "metal_doom.h"

void __real_doomgeneric_Tick(void);

extern boolean setsizeneeded;
void R_ExecuteSetViewSize(void);
void D_Display(void);

extern boolean inhelpscreens;

static int s_wipe_active;

int
metal_doom_wipe_active(void)
{
	return s_wipe_active;
}

void
metal_doom_wipe_set_active(int active)
{
	s_wipe_active = active ? 1 : 0;
}

static void
MetalWipeAdvance(void)
{
	boolean done;

	done = wipe_ScreenWipe(wipe_Melt, 0, 0, SCREENWIDTH, SCREENHEIGHT, 1);
	I_UpdateNoBlit();
	M_Drawer();
	I_FinishUpdate();
	if (done) {
		s_wipe_active = 0;
	}
}

void
__wrap_D_Display(void)
{
	static boolean viewactivestate = false;
	static boolean menuactivestate = false;
	static boolean inhelpscreensstate = false;
	static boolean fullscreen = false;
	static gamestate_t oldgamestate = -1;
	static int borderdrawcount;
	int y;
	boolean wipe;
	boolean redrawsbar;

	if (s_wipe_active) {
		MetalWipeAdvance();
		return;
	}

	if (nodrawers) {
		return;
	}

	redrawsbar = false;

	if (setsizeneeded) {
		R_ExecuteSetViewSize();
		oldgamestate = -1;
		borderdrawcount = 3;
	}

	if (gamestate != wipegamestate) {
		wipe = true;
		wipe_StartScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);
	} else {
		wipe = false;
	}

	if (gamestate == GS_LEVEL && gametic) {
		HU_Erase();
	}

	switch (gamestate) {
	case GS_LEVEL:
		if (!gametic) {
			break;
		}
		if (automapactive) {
			AM_Drawer();
		}
		if (wipe || (viewheight != 200 && fullscreen)) {
			redrawsbar = true;
		}
		if (inhelpscreensstate && !inhelpscreens) {
			redrawsbar = true;
		}
		ST_Drawer(viewheight == 200, redrawsbar);
		fullscreen = viewheight == 200;
		break;

	case GS_INTERMISSION:
		WI_Drawer();
		break;

	case GS_FINALE:
		F_Drawer();
		break;

	case GS_DEMOSCREEN:
		D_PageDrawer();
		break;
	}

	I_UpdateNoBlit();

	if (gamestate == GS_LEVEL && !automapactive && gametic) {
		R_RenderPlayerView(&players[displayplayer]);
	}

	if (gamestate == GS_LEVEL && gametic) {
		HU_Drawer();
	}

	if (gamestate != oldgamestate && gamestate != GS_LEVEL) {
		I_SetPalette(W_CacheLumpName(DEH_String("PLAYPAL"), PU_CACHE));
	}

	if (gamestate == GS_LEVEL && oldgamestate != GS_LEVEL) {
		viewactivestate = false;
		R_FillBackScreen();
	}

	if (gamestate == GS_LEVEL && !automapactive && scaledviewwidth != 320) {
		if (menuactive || menuactivestate || !viewactivestate) {
			borderdrawcount = 3;
		}
		if (borderdrawcount) {
			R_DrawViewBorder();
			borderdrawcount--;
		}
	}

	if (testcontrols) {
		V_DrawMouseSpeedBox(testcontrols_mousespeed);
	}

	menuactivestate = menuactive;
	viewactivestate = viewactive;
	inhelpscreensstate = inhelpscreens;
	oldgamestate = wipegamestate = gamestate;

	if (paused) {
		if (automapactive) {
			y = 4;
		} else {
			y = viewwindowy + 4;
		}
		V_DrawPatchDirect(viewwindowx + (scaledviewwidth - 68) / 2, y,
				  W_CacheLumpName(DEH_String("M_PAUSE"), PU_CACHE));
	}

	M_Drawer();
	NetUpdate();

	if (!wipe) {
		I_FinishUpdate();
		return;
	}

	/* One melt frame; continue across Ticks via s_wipe_active. */
	wipe_EndScreen(0, 0, SCREENWIDTH, SCREENHEIGHT);
	{
		boolean done;

		done = wipe_ScreenWipe(wipe_Melt, 0, 0, SCREENWIDTH, SCREENHEIGHT, 1);
		I_UpdateNoBlit();
		M_Drawer();
		I_FinishUpdate();
		s_wipe_active = done ? 0 : 1;
	}
}

void
__wrap_doomgeneric_Tick(void)
{
	if (s_wipe_active) {
		/* Freeze sim while melt continues. */
		D_Display();
		return;
	}

	__real_doomgeneric_Tick();
}
