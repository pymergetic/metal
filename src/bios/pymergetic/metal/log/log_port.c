/** @file
  BIOS body for the one EDK2 primitive log.c needs: writing a line to the
  UEFI-ConOut-shaped viewport. No real UEFI text console exists under
  Multiboot2/BIOS — the shim's Print() is a stub, kept only so this
  viewport's call shape matches the EFI port.
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
