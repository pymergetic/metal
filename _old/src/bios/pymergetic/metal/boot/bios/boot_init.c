/** @file
  BIOS boot port hooks (floor deltas + handoff marker).
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/dev/input/input.h>

#include <PmBiosUefi.h>

void
pm_metal_boot_port_floor (
  CONST CHAR8  **fs_compat,
  CONST CHAR8  **random_compat
  )
{
  if (fs_compat != NULL) {
    *fs_compat = "embed";
  }

  if (random_compat != NULL) {
    *random_compat = "rdrand-or-weak";
  }

  if (pm_metal_input_ps2_init () != 0) {
    pm_metal_log ("metal-boot: ps2 init failed (keyboard may be dead)");
  }
}

void
pm_metal_boot_port_seed (
  VOID
  )
{
  pm_metal_log ("+-- handoff      ok");
}
