/*
 * Lower half — physical scanout only.
 *
 * Agnostic to upper half (surfaces/widgets). Probe picks a backend; present
 * is copy/flip work (+ optional yield between bands). Never busy-waits.
 * Host-only.
 */
#ifndef PYMERGETIC_METAL_DEV_GFX_SCANOUT_H_
#define PYMERGETIC_METAL_DEV_GFX_SCANOUT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

#define PM_METAL_SCANOUT_CAP_TEAR_FREE  (1u << 0)
#define PM_METAL_SCANOUT_CAP_CHUNKED    (1u << 1)
#define PM_METAL_SCANOUT_CAP_DIRECT     (1u << 2)

typedef struct {
	uint32_t *shadow;
	uint32_t  shadow_w;
	uint32_t  shadow_h;
	uint32_t  shadow_pitch;
	uint32_t *fb;
	uint32_t  fb_ppsl;
	uint32_t  mode_w;
	uint32_t  mode_h;
	void     *gop;   /* EFI_GRAPHICS_OUTPUT_PROTOCOL* or NULL */
	int       owned; /* 1 = post-EBS / BIOS owned */
} pm_metal_scanout_bind_t;

typedef struct pm_metal_scanout_ops {
	const char *name;
	/** 0 = accept this backend. */
	int (*probe)(const pm_metal_scanout_bind_t *b);
	int (*present_rect)(int32_t x, int32_t y, int32_t w, int32_t h);
	/** 0 = finished sync; 1 = async job armed. */
	int (*job_begin)(int32_t x, int32_t y, int32_t w, int32_t h);
	/** 0 = done; 1 = more work; -1 = error. */
	int (*job_step)(void);
	uint32_t (*caps)(void);
	/**
	 * If CAP_DIRECT: replace *pixels with scanout back buffer (caller
	 * frees prior heap shadow). Returns 0 ok.
	 */
	int (*adopt_shadow)(uint32_t **pixels, uint32_t *pitch);
	/** After flip with direct shadow — point compositor at next back. */
	void (*after_flip)(uint32_t **pixels);
	void (*fini)(void);
} pm_metal_scanout_ops_t;

/** Probe order + bind. 0 ok. */
int pm_metal_scanout_bind(const pm_metal_scanout_bind_t *b);

const pm_metal_scanout_ops_t *pm_metal_scanout_ops(void);

const char *pm_metal_scanout_name(void);

uint32_t pm_metal_scanout_caps(void);

void pm_metal_scanout_fini(void);

/** Live bind (shadow pointer may change after adopt_shadow). */
const pm_metal_scanout_bind_t *pm_metal_scanout_bind_info(void);
void pm_metal_scanout_bind_set_shadow(uint32_t *pixels, uint32_t pitch);

/** Shared: copy shadow rect → dst (pitch in pixels). */
void pm_metal_scanout_copy_rect(uint32_t *dst, uint32_t dst_pitch, int32_t x,
				int32_t y, int32_t w, int32_t h,
				const pm_metal_scanout_bind_t *b);

/* Backend ops tables (probe via bind). */
extern const pm_metal_scanout_ops_t  g_pm_metal_scanout_virtio_gpu;
extern const pm_metal_scanout_ops_t  g_pm_metal_scanout_bochs;
extern const pm_metal_scanout_ops_t  g_pm_metal_scanout_radeon_rv370;
extern const pm_metal_scanout_ops_t  g_pm_metal_scanout_i915_855gm; /* sample */
extern const pm_metal_scanout_ops_t  g_pm_metal_scanout_gop_blt;
extern const pm_metal_scanout_ops_t  g_pm_metal_scanout_lfb_copy;

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_GFX_SCANOUT_H_ */
