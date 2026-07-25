/** @file
  EFI body for the one EDK2 primitive log.c needs: writing a line to the
  real UEFI ConOut text console (pre-ExitBootServices only).
**/

#include <Uefi.h>
#include <Library/UefiLib.h>

void
pm_metal_log_port_emit_uefi (
  const char  *line
  )
{
  Print (L"%a\r\n", line);
}
