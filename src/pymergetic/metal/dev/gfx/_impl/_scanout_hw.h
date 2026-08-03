/*
 * Private C border for HW scanout ports (i915 / radeon).
 * Bind layout must match Rust `scanout::Bind` (#[repr(C)]).
 */
#ifndef PYMERGETIC_METAL_DEV_GFX_SCANOUT_HW_H_
#define PYMERGETIC_METAL_DEV_GFX_SCANOUT_HW_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_SCANOUT_CAP_TEAR_FREE (1u << 0)
#define PM_METAL_SCANOUT_CAP_CHUNKED   (1u << 1)
#define PM_METAL_SCANOUT_CAP_DIRECT    (1u << 2)

typedef struct {
  uint32_t *shadow;
  uint32_t  shadow_w;
  uint32_t  shadow_h;
  uint32_t  shadow_pitch;
  uint32_t *fb;
  uint32_t  fb_ppsl;
  uint32_t  mode_w;
  uint32_t  mode_h;
  void     *gop;
  int32_t   owned;
} pm_metal_scanout_bind_t;

/** Live bind from Rust scanout dispatch. */
const pm_metal_scanout_bind_t *pm_metal_dev_gfx_scanout_bind_info(void);

/* Bodies in _scanout_hw.c (shared by i915/radeon ports). */
void pm_metal_mem_fence(void);
void pm_metal_cpu_pause(void);

/* i915 855GM */
int32_t  pm_metal_dev_gfx_scanout_i915_probe(const pm_metal_scanout_bind_t *b);
int32_t  pm_metal_dev_gfx_scanout_i915_present_rect(int32_t x, int32_t y, int32_t w, int32_t h);
int32_t  pm_metal_dev_gfx_scanout_i915_job_begin(int32_t x, int32_t y, int32_t w, int32_t h);
int32_t  pm_metal_dev_gfx_scanout_i915_job_step(void);
uint32_t pm_metal_dev_gfx_scanout_i915_caps(void);
void     pm_metal_dev_gfx_scanout_i915_fini(void);

/* radeon RV370 */
int32_t  pm_metal_dev_gfx_scanout_radeon_probe(const pm_metal_scanout_bind_t *b);
int32_t  pm_metal_dev_gfx_scanout_radeon_present_rect(int32_t x, int32_t y, int32_t w, int32_t h);
int32_t  pm_metal_dev_gfx_scanout_radeon_job_begin(int32_t x, int32_t y, int32_t w, int32_t h);
int32_t  pm_metal_dev_gfx_scanout_radeon_job_step(void);
uint32_t pm_metal_dev_gfx_scanout_radeon_caps(void);
void     pm_metal_dev_gfx_scanout_radeon_fini(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_GFX_SCANOUT_HW_H_ */
