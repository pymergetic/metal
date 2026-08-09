#ifndef PYMERGETIC_METAL_DEV_GFX_COMPOSITOR_H_
#define PYMERGETIC_METAL_DEV_GFX_COMPOSITOR_H_
#include <stdint.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Re-declare compositor API for seat-path counting (same symbols as gfx.h). */
int pm_metal_gfx_init_from_bind(const pm_metal_scanout_bind_t *harvest);
int pm_metal_gfx_init(void);
void pm_metal_gfx_fini(void);
int pm_metal_gfx_ready(void);
pm_metal_gfx_surface_t *pm_metal_gfx_surface(void);
const char *pm_metal_gfx_scanout_name(void);
void pm_metal_gfx_clear(pm_metal_gfx_color_t color);
void pm_metal_gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h, pm_metal_gfx_color_t color);
int pm_metal_gfx_present_rect(int32_t x, int32_t y, int32_t w, int32_t h);
int pm_metal_gfx_present(void);
#ifdef __cplusplus
}
#endif
#endif
