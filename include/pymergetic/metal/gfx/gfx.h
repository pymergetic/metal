/*
 * Metal graphics — guest/host dual ABI (WASI-style imports on wasm32).
 *
 * Shadow FB is BGRA8888. Guests draw via these calls; no raw pixel pointer
 * crosses the wasm boundary. init/fini/ready are host-only.
 */
#ifndef PYMERGETIC_METAL_GFX_H_
#define PYMERGETIC_METAL_GFX_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_gfx_color_t;

#define PM_METAL_GFX_RGBA(r, g, b, a)                                      \
	((pm_metal_gfx_color_t)(((uint32_t)(b)&0xffu)                      \
				| (((uint32_t)(g)&0xffu) << 8)             \
				| (((uint32_t)(r)&0xffu) << 16)            \
				| (((uint32_t)(a)&0xffu) << 24)))

#define PM_METAL_GFX_RGB(r, g, b) PM_METAL_GFX_RGBA((r), (g), (b), 0xff)

#define PM_METAL_GFX_WASI_MODULE "pymergetic.metal.gfx"

typedef uint32_t pm_metal_gfx_surface_h;

#define PM_METAL_GFX_SURFACE_INVALID 0u
#define PM_METAL_GFX_SURFACE_DEFAULT 1u /* full GOP / active renderer primary */

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_GFX_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_GFX_WASI_MODULE, name)
#endif

#if !defined(__wasm__)
typedef struct {
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	uint32_t pitch;
} pm_metal_gfx_surface_t;

/**
 * Pre-EBS: LocateProtocol GOP + stash FB base/size (Boot Services).
 * Safe to call once on the sync floor.
 */
int pm_metal_gfx_harvest(void);

/**
 * Bind shadow FB and mark ready. Uses harvest; may run post-EBS.
 * Calls harvest itself if still pre-EBS and harvest was skipped.
 */
int pm_metal_gfx_init(void);
void pm_metal_gfx_fini(void);
int pm_metal_gfx_ready(void);
int pm_metal_gfx_harvested(void);
pm_metal_gfx_surface_t *pm_metal_gfx_surface(void);

/** Allocate a named surface (tab content). Handle ≥ 2. */
pm_metal_gfx_surface_h pm_metal_gfx_surface_alloc(void);
void pm_metal_gfx_surface_free(pm_metal_gfx_surface_h s);
void pm_metal_gfx_surface_set_rect(pm_metal_gfx_surface_h s, int32_t x,
				   int32_t y, int32_t w, int32_t h);
/** Present only that surface's rect (DEFAULT = full). */
int pm_metal_gfx_present_surface(pm_metal_gfx_surface_h s);
#endif

#if defined(__wasm__)
extern int pm_metal_gfx_width(void) PM_METAL_GFX_IMPORT(pm_metal_gfx_width);
extern int pm_metal_gfx_height(void) PM_METAL_GFX_IMPORT(pm_metal_gfx_height);
extern void pm_metal_gfx_set_surface(pm_metal_gfx_surface_h s)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_set_surface);
extern pm_metal_gfx_surface_h pm_metal_gfx_draw_surface(void)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_draw_surface);
extern int32_t pm_metal_gfx_surface_width(pm_metal_gfx_surface_h s)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_surface_width);
extern int32_t pm_metal_gfx_surface_height(pm_metal_gfx_surface_h s)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_surface_height);
extern int32_t pm_metal_gfx_surface_origin_x(pm_metal_gfx_surface_h s)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_surface_origin_x);
extern int32_t pm_metal_gfx_surface_origin_y(pm_metal_gfx_surface_h s)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_surface_origin_y);
extern void pm_metal_gfx_clear(pm_metal_gfx_color_t color)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_clear);
extern void pm_metal_gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h,
				   pm_metal_gfx_color_t color)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_fill_rect);
extern void pm_metal_gfx_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h,
				   pm_metal_gfx_color_t color)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_draw_rect);
extern void pm_metal_gfx_bevel_rect(int32_t x, int32_t y, int32_t w, int32_t h,
				    int raised, pm_metal_gfx_color_t hi,
				    pm_metal_gfx_color_t lo)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_bevel_rect);
extern void pm_metal_gfx_draw_text(int32_t x, int32_t y, const char *text,
				   pm_metal_gfx_color_t fg,
				   pm_metal_gfx_color_t bg, int transparent_bg)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_draw_text);
extern uint32_t pm_metal_gfx_font_width(void)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_font_width);
extern uint32_t pm_metal_gfx_font_height(void)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_font_height);
extern int pm_metal_gfx_present(void) PM_METAL_GFX_IMPORT(pm_metal_gfx_present);
extern int pm_metal_gfx_present_rect(int32_t x, int32_t y, int32_t w, int32_t h)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_present_rect);
/**
 * Nearest-neighbor scale blit from guest BGRA/XRGB buffer (src_pitch bytes/row)
 * into the shadow FB covering dest (dx,dy,dw,dh), then present that rect.
 */
extern int pm_metal_gfx_blit_bgra(int32_t dx, int32_t dy, int32_t dw, int32_t dh,
				  const void *pixels, int32_t src_w,
				  int32_t src_h, int32_t src_pitch)
	PM_METAL_GFX_IMPORT(pm_metal_gfx_blit_bgra);
#else
int pm_metal_gfx_width(void);
int pm_metal_gfx_height(void);
/** Draw target: coords relative to surface; width/height/clear/present follow it. */
void pm_metal_gfx_set_surface(pm_metal_gfx_surface_h s);
pm_metal_gfx_surface_h pm_metal_gfx_draw_surface(void);
int32_t pm_metal_gfx_surface_width(pm_metal_gfx_surface_h s);
int32_t pm_metal_gfx_surface_height(pm_metal_gfx_surface_h s);
int32_t pm_metal_gfx_surface_origin_x(pm_metal_gfx_surface_h s);
int32_t pm_metal_gfx_surface_origin_y(pm_metal_gfx_surface_h s);
void pm_metal_gfx_clear(pm_metal_gfx_color_t color);
void pm_metal_gfx_fill_rect(int32_t x, int32_t y, int32_t w, int32_t h,
			    pm_metal_gfx_color_t color);
void pm_metal_gfx_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h,
			    pm_metal_gfx_color_t color);
void pm_metal_gfx_bevel_rect(int32_t x, int32_t y, int32_t w, int32_t h,
			     int raised, pm_metal_gfx_color_t hi,
			     pm_metal_gfx_color_t lo);
void pm_metal_gfx_draw_text(int32_t x, int32_t y, const char *text,
			    pm_metal_gfx_color_t fg, pm_metal_gfx_color_t bg,
			    int transparent_bg);
uint32_t pm_metal_gfx_font_width(void);
uint32_t pm_metal_gfx_font_height(void);
int pm_metal_gfx_present(void);
int pm_metal_gfx_present_rect(int32_t x, int32_t y, int32_t w, int32_t h);
int pm_metal_gfx_blit_bgra(int32_t dx, int32_t dy, int32_t dw, int32_t dh,
			   const void *pixels, int32_t src_w, int32_t src_h,
			   int32_t src_pitch);

/** Register wasi-style natives. Returns 0 ok, -1 fail. */
int pm_metal_gfx_native_register(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GFX_H_ */
