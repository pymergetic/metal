/** @file
  exp2 EFI: virtio DMA pages defer to memalign fallback (post-EBS safe).
  Bringup opens virtio-net after ExitBootServices; skip gBS->AllocatePages.
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
