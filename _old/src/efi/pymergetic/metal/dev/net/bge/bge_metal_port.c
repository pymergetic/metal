/** @file
  EFI body for the one EDK2 primitive bge_metal.c needs: DMA-coherent page
  allocation via gBS->AllocatePages, pre-ExitBootServices.
**/

#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>

#include <stdint.h>

#define PM_METAL_BGE_SIZE_TO_PAGES(b) (((b) + EFI_PAGE_SIZE - 1) / EFI_PAGE_SIZE)

void *pm_metal_bge_port_alloc_pages(uintptr_t bytes)
{
  EFI_PHYSICAL_ADDRESS pa;
  EFI_STATUS           st;
  UINTN                pages;

  if (gBS == NULL) {
    return NULL;
  }

  pages = PM_METAL_BGE_SIZE_TO_PAGES(bytes);
  st    = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &pa);
  if (EFI_ERROR(st)) {
    return NULL;
  }

  return (void *)(uintptr_t)pa;
}
