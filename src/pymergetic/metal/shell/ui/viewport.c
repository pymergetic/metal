/* UI viewport: console bytes → VT/F1 cells → draw surface → present. */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/console.h"
#include "pymergetic/metal/dev/gfx/gfx.h"
#include "pymergetic/metal/draw.h"
#include "pymergetic/metal/shell/vt/__init__.h"

#include "pymergetic/metal/shell/ui.h"

static pm_metal_draw_surface_t g_ds;
static int g_bound;
static pm_metal_console_vp_id g_vp = -1;

static void ui_sink(const uint8_t *data, size_t n, void *user)
{
    (void)user;
    if (data == NULL || n == 0 || !g_bound) {
        return;
    }
    pm_metal_vt_write((const char *)data, n);
    (void)pm_metal_vt_render(pm_metal_vt_active());
    (void)pm_metal_shell_ui_present();
}

int pm_metal_shell_ui_attach_console0(void)
{
    pm_metal_gfx_surface_t *gs;

    if (!pm_metal_gfx_ready()) {
        return -1;
    }
    gs = pm_metal_gfx_surface();
    if (gs == NULL || gs->pixels == NULL) {
        return -1;
    }

    if (pm_metal_vt_init() != 0) {
        return -1;
    }

    /* Bind without soft_init (it memset-clears and ignores pitch). */
    g_ds.pixels = (uint8_t *)gs->pixels;
    g_ds.width = gs->width;
    g_ds.height = gs->height;
    g_ds.bpp = 32;
    g_ds.stride = gs->pitch * 4u;

    pm_metal_vt_bind_surface(0, &g_ds);
    (void)pm_metal_vt_switch(0);
    g_bound = 1;

    g_vp = pm_metal_console_viewport_attach(0, ui_sink, NULL);
    if (g_vp < 0) {
        g_bound = 0;
        return -1;
    }

    pm_metal_gfx_clear(PM_METAL_GFX_RGB(0x10, 0x14, 0x1a));
    pm_metal_gfx_draw_text(8, 8, "MetalPython", PM_METAL_GFX_RGB(0x6e, 0xc8, 0xff),
                           PM_METAL_GFX_RGB(0x10, 0x14, 0x1a), 0);
    (void)pm_metal_shell_ui_present();
    return 0;
}

int pm_metal_shell_ui_present(void)
{
    if (!pm_metal_gfx_ready()) {
        return -1;
    }
    return pm_metal_gfx_present();
}
