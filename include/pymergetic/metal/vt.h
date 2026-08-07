#ifndef PM_METAL_VT_H_
#define PM_METAL_VT_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/draw.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PM_METAL_VT_COUNT
#define PM_METAL_VT_COUNT 7 /* F1–F6 shells + F7 dashboard */
#endif

#ifndef PM_METAL_VT_COLS
#define PM_METAL_VT_COLS 80
#endif

#ifndef PM_METAL_VT_ROWS
#define PM_METAL_VT_ROWS 25
#endif

/* Cell grid + optional DrawSurface for FB text shells. */
typedef struct {
    char cells[PM_METAL_VT_ROWS][PM_METAL_VT_COLS];
    int32_t cursor_c;
    int32_t cursor_r;
    pm_metal_draw_surface_t *ds; /* optional soft/FB surface */
} pm_metal_vt_t;

int32_t pm_metal_vt_init(void);
int32_t pm_metal_vt_ready(void);

/* 0..PM_METAL_VT_COUNT-1 ↔ F1..Fn */
int32_t pm_metal_vt_switch(int32_t index);
int32_t pm_metal_vt_active(void);

pm_metal_vt_t *pm_metal_vt_get(int32_t index);

void pm_metal_vt_bind_surface(int32_t index, pm_metal_draw_surface_t *ds);

/* Write to active VT cell grid (sync). Optionally dirty-blit glyphs to DS. */
void pm_metal_vt_write(const char *s, size_t n);
void pm_metal_vt_puts(const char *s);

/* Render full cell grid of a VT onto its bound DrawSurface (8×8 glyphs). */
int32_t pm_metal_vt_render(int32_t index);

#ifdef __cplusplus
}
#endif

#endif
