/*
 * EFI guest smoke — Metal ui + gfx imports (surface-clipped draw).
 */
#include "pymergetic/metal/boot/authors.h"
#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/shell/shell/shell.h"
#include "pymergetic/metal/shell/ui/ui.h"
#include "pymergetic/metal/guest/mod/mod.h"
#include "pymergetic/metal/runtime/async/async.h"

pm_metal_status_t ui_hello_run(pm_metal_async_handle_t self_h)
{
  pm_metal_ui_handle_t   tab;
  pm_metal_gfx_surface_h surf;

  (void)self_h;
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
    pm_metal_gfx_draw_text(32,
                           32,
                           "Metal gfx",
                           PM_METAL_GFX_RGB(0xff, 0xff, 0xff),
                           PM_METAL_GFX_RGB(0x40, 0x20, 0x60),
                           0);
    (void)pm_metal_gfx_present();
  }

  pm_metal_gfx_set_surface(PM_METAL_GFX_SURFACE_DEFAULT);
  return PM_METAL_DONE;
}

int32_t pm_metal_mod_on_load(void)
{
  pm_metal_mod_set_about_kernel();

  if (pm_metal_mod_register_func("run", "ui_hello_run") != 0) {
    return -1;
  }

  if (pm_metal_mod_register_cmd("ui_hello", "run", "ui hello proof") != 0) {
    return -1;
  }

  return 0;
}

int32_t pm_metal_mod_on_unload(void)
{
  return 0;
}

int main(void)
{
  return 0;
}
