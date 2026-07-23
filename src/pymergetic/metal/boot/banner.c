/** @file
  Boot / shutdown ASCII banners (shared host).
  (impl: common)
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/util/ascii.h>
#include <pymergetic/metal/dev/gfx/gfx.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseLib.h>

#ifndef METAL_BOOT_DEAD_HOLD_MS
#define METAL_BOOT_DEAD_HOLD_MS  500u
#endif

#ifndef METAL_BOOT_FADE_MS
#define METAL_BOOT_FADE_MS       2500u /* ~2.5 s fade to black before reset */
#endif

#ifndef METAL_BOOT_FADE_STEPS
#define METAL_BOOT_FADE_STEPS    20u
#endif

void
pm_metal_boot_banner (
  void
  )
{
  pm_metal_util_ascii_log_styled (PM_METAL_LOG_STYLE_ACCENT, "METAL");
}

/**
 * Darken shadow FB toward black and present. Full-frame — shutdown only.
 */
STATIC
VOID
MetalBootFadeToBlack (
  UINT32  total_ms
  )
{
  pm_metal_gfx_surface_t  *surf;
  UINT32                   step;
  UINT32                   step_ms;
  UINT32                   y;
  UINT32                   x;
  UINT32                   w;
  UINT32                   h;
  UINT32                   pitch;

  surf = pm_metal_gfx_surface ();
  if (surf == NULL || surf->pixels == NULL || surf->width == 0
      || surf->height == 0)
  {
    pm_metal_time_msleep (total_ms);
    return;
  }

  w        = surf->width;
  h        = surf->height;
  pitch    = surf->pitch;
  step_ms  = total_ms / METAL_BOOT_FADE_STEPS;
  if (step_ms < 16u) {
    step_ms = 16u;
  }

  for (step = 0; step < METAL_BOOT_FADE_STEPS; step++) {
    /*
     * Geometric approach to black: each step keeps ~88% of remaining
     * brightness so the last frames land near black without a hard cut.
     */
    for (y = 0; y < h; y++) {
      UINT32  *row;

      row = &surf->pixels[y * pitch];
      for (x = 0; x < w; x++) {
        UINT32  c;
        UINT32  r;
        UINT32  g;
        UINT32  b;

        c      = row[x];
        r      = ((c >> 16) & 0xffu) * 7u / 8u;
        g      = ((c >> 8) & 0xffu) * 7u / 8u;
        b      = (c & 0xffu) * 7u / 8u;
        row[x] = (c & 0xff000000u) | (r << 16) | (g << 8) | b;
      }
    }

    (VOID)pm_metal_gfx_present_surface (PM_METAL_GFX_SURFACE_DEFAULT);
    pm_metal_time_msleep (step_ms);
  }

  pm_metal_gfx_clear (PM_METAL_GFX_RGB (0, 0, 0));
  (VOID)pm_metal_gfx_present_surface (PM_METAL_GFX_SURFACE_DEFAULT);
}

void
pm_metal_boot_dead (
  int  reboot
  )
{
  pm_metal_log ("");
  pm_metal_util_ascii_log_styled (PM_METAL_LOG_STYLE_FAIL, "DEAD");
  pm_metal_log ("");
  pm_metal_log (
    reboot ? " metal: system down - restarting"
           : " metal: system down"
    );
  pm_metal_ui_set_status (reboot ? "restarting…" : "system down");
  (VOID)pm_metal_ui_frame ();
  (VOID)pm_metal_gfx_present_surface (PM_METAL_GFX_SURFACE_DEFAULT);

  /* Let the banner land on-screen before the fade. */
  pm_metal_time_msleep (METAL_BOOT_DEAD_HOLD_MS);

  /*
   * ThinkPad has no serial — hold the DEAD tree visually, then fade 2–3 s
   * so power-off does not feel like an instant black cut.
   */
  MetalBootFadeToBlack (METAL_BOOT_FADE_MS);
  (VOID)reboot;
}
