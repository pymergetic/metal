/*
 * EFI guest smoke — Metal ui + gfx imports (surface-clipped draw).
 */
#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/shell/shell/shell.h"
#include "pymergetic/metal/shell/ui/ui.h"

int
main(void)
{
	pm_metal_ui_handle_t     tab;
	pm_metal_gfx_surface_h   surf;

	pm_metal_shell_log("t_ui_hello: via shell_log");
	pm_metal_ui_active_puts("t_ui_hello: active tab line");
	pm_metal_ui_set_status("ui_hello running");

	tab  = pm_metal_ui_tab_active();
	surf = pm_metal_ui_tab_surface(tab);
	if (surf != PM_METAL_GFX_SURFACE_INVALID) {
		pm_metal_gfx_set_surface(surf);
	}

	if (pm_metal_gfx_width() > 0 && pm_metal_gfx_height() > 0) {
		pm_metal_gfx_clear(PM_METAL_GFX_RGB(0x1a, 0x1a, 0x22));
		pm_metal_gfx_fill_rect(24, 24, 160, 32, PM_METAL_GFX_RGB(0x40, 0x20, 0x60));
		pm_metal_gfx_draw_text(32, 32, "Metal gfx", PM_METAL_GFX_RGB(0xff, 0xff, 0xff),
				       PM_METAL_GFX_RGB(0x40, 0x20, 0x60), 0);
		(void)pm_metal_gfx_present();
	}

	pm_metal_gfx_set_surface(PM_METAL_GFX_SURFACE_DEFAULT);
	return 0;
}
