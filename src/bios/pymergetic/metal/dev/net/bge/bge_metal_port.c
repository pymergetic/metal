/** @file
  BIOS body for the one EDK2 primitive bge_metal.c needs: page allocation.
  No boot-services page pool exists under Multiboot2/BIOS — always defer to
  the arena.
**/

#include <stddef.h>
#include <stdint.h>

void *pm_metal_bge_port_alloc_pages(uintptr_t bytes)
{
  (void)bytes;
  return NULL;
}
