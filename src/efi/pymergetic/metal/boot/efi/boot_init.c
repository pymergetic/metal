/** @file
  EFI boot port hooks (floor deltas + EBS marker).
**/
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/log/log.h>

#include <Uefi.h>

void
pm_metal_boot_port_floor (
  CONST CHAR8  **fs_compat,
  CONST CHAR8  **random_compat
  )
{
  if (fs_compat != NULL) {
    *fs_compat = "esp";
  }

  if (random_compat != NULL) {
    *random_compat = "efi-or-weak";
  }
}

void
pm_metal_boot_port_seed (
  VOID
  )
{
  pm_metal_log ("+-- ebs          ok");
}
