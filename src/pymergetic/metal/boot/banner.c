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

void
pm_metal_boot_banner (
  void
  )
{
  pm_metal_util_ascii_log ("METAL");
  pm_metal_log ("                 pymergetic metal");
}

void
pm_metal_boot_dead (
  int  reboot
  )
{
  pm_metal_log ("");
  pm_metal_util_ascii_log ("DEAD");
  pm_metal_log ("");
  pm_metal_log (
    reboot ? " metal: system down - restarting"
           : " metal: system down"
    );
  (VOID)pm_metal_ui_frame ();
  (VOID)pm_metal_gfx_present ();

  /*
   * Hidden hold so the banner is readable before reset tears the FB
   * down. Reboot needs longer — cold reset is immediate once called.
   */
  pm_metal_time_msleep (reboot ? 500u : 250u);
}
