#ifndef PM_METAL_TUI_H_
#define PM_METAL_TUI_H_

#include <stdint.h>

#include "pymergetic/metal/draw.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PM_METAL_TUI_VT_INDEX
#define PM_METAL_TUI_VT_INDEX 6 /* F7 dashboard (F1–F6 = shells) */
#endif

int32_t pm_metal_tui_init(void);

/* Fill VT cell grid with static DOS-Edit-style dashboard (menu + 2×2 panes + footer). */
int32_t pm_metal_tui_paint_vt(int32_t vt_index);

/* Paint + render bound DrawSurface via VT glyph blit. */
int32_t pm_metal_tui_render_vt(int32_t vt_index);

/* Direct soft-DrawSurface render (same layout, no VT cells). */
int32_t pm_metal_tui_render_draw(pm_metal_draw_surface_t *ds);

#ifdef __cplusplus
}
#endif

#endif
