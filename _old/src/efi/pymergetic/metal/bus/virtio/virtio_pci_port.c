/** @file
  EFI body for the one EDK2 primitive virtio_pci.c needs: page allocation
  via real UEFI boot services (pre-ExitBootServices only). Falls back to the
  arena (via the caller) once gBS is gone or unavailable.
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

#include <stdbool.h>

void *pm_metal_virtio_port_alloc_pages(unsigned pages)
{
  EFI_PHYSICAL_ADDRESS Pa;
  EFI_STATUS            Status;

  if (gBS == NULL) {
    return NULL;
  }

  Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, (UINTN)pages, &Pa);
  if (EFI_ERROR(Status)) {
    return NULL;
  }

  return (void *)(UINTN)Pa;
}

bool pm_metal_virtio_port_free_pages(void *buf, unsigned pages)
{
  if (gBS == NULL) {
    return false;
  }

  (void)gBS->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)buf, (UINTN)pages);
  return true;
}
