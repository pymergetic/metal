#ifndef PM_METAL_GFX_BRINGUP_H_
#define PM_METAL_GFX_BRINGUP_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Harvest → scanout_bind → UI viewport on console #0. 0 = panel lit. */
int pm_metal_gfx_bringup(void);

#ifdef __cplusplus
}
#endif

#endif
