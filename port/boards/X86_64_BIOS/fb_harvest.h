#ifndef PM_METAL_BIOS_FB_HARVEST_H_
#define PM_METAL_BIOS_FB_HARVEST_H_

#include "pymergetic/metal/dev/gfx/scanout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fill bind from Bochs VBE and/or Multiboot framebuffer. 0 = ok. */
int pm_metal_bios_fb_harvest(pm_metal_scanout_bind_t *out);

#ifdef __cplusplus
}
#endif

#endif
