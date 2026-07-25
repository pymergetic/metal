/** @file
  BIOS body for the one EDK2 primitive virtio_pci.c needs: page allocation.
  No boot-services page pool exists under Multiboot2/BIOS — always defer to
  the arena.
**/

#include <stdbool.h>
#include <stddef.h>

void *pm_metal_virtio_port_alloc_pages(unsigned pages)
{
  (void)pages;
  return NULL;
}

bool pm_metal_virtio_port_free_pages(void *buf, unsigned pages)
{
  (void)buf;
  (void)pages;
  return false;
}
