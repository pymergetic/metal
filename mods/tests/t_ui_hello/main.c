/*
 * EFI guest smoke — Metal ui + gfx imports (not plain WASI printf).
 */
#include "pymergetic/metal/gfx.h"
#include "pymergetic/metal/shell.h"
#include "pymergetic/metal/ui.h"

int
main(void)
{
	pm_metal_ui_handle_t h;

	pm_metal_shell_log("t_ui_hello: via shell_log");
	pm_metal_ui_active_puts("t_ui_hello: active tab line");
	pm_metal_ui_set_status("ui_hello running");

	h = pm_metal_ui_console_handle();
	(void)h;

	if (pm_metal_gfx_width() > 0 && pm_metal_gfx_height() > 0) {
		pm_metal_gfx_fill_rect(24, 24, 160, 32, PM_METAL_GFX_RGB(0x40, 0x20, 0x60));
		pm_metal_gfx_draw_text(32, 32, "Metal gfx", PM_METAL_GFX_RGB(0xff, 0xff, 0xff),
				       PM_METAL_GFX_RGB(0x40, 0x20, 0x60), 0);
		(void)pm_metal_gfx_present();
	}

	return 0;
}
