#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/mem_map.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <Uefi.h>
#include <Protocol/LoadedImage.h>

#include <stddef.h>
#include <stdint.h>

#include <pymergetic/metal/boot/platform/mem_map.h>

#include "efi_ctx.h"

#define MAX_REGIONS 128u

/* EFI_LOADED_IMAGE_PROTOCOL_GUID — avoid depending on EDK2 GUID .obj */
static EFI_GUID g_loaded_image_guid = {
  0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B }
};

static pm_metal_boot_mem_region_t g_regions[MAX_REGIONS];
static uint32_t g_n_regions;
static uintptr_t g_image_base;
static uintptr_t g_image_end;
static int g_cached;

static uint32_t map_efi_type(UINT32 t)
{
  /*
   * Free RAM for the boot tree / OS view:
   *   Conventional + BootServices* (reclaimable after ExitBootServices).
   * Not free: Loader* (image + our AllocatePages arena), Runtime*, ACPI*, …
   * Mapping Loader* as AVAILABLE made the tree dump dozens of tiny highmem
   * chips and duplicate kernel/area.
   */
  if (t == EfiConventionalMemory || t == EfiBootServicesCode || t == EfiBootServicesData) {
    return (uint32_t)PM_METAL_BOOT_MEM_AVAILABLE;
  }
  if (t == EfiACPIReclaimMemory) {
    return (uint32_t)PM_METAL_BOOT_MEM_ACPI_RECLAIM;
  }
  if (t == EfiACPIMemoryNVS) {
    return (uint32_t)PM_METAL_BOOT_MEM_ACPI_NVS;
  }
  if (t == EfiReservedMemoryType || t == EfiRuntimeServicesCode || t == EfiRuntimeServicesData
      || t == EfiMemoryMappedIO || t == EfiMemoryMappedIOPortSpace || t == EfiPalCode
      || t == EfiUnusableMemory || t == EfiLoaderCode || t == EfiLoaderData) {
    return (uint32_t)PM_METAL_BOOT_MEM_RESERVED;
  }
  return (uint32_t)PM_METAL_BOOT_MEM_OTHER;
}

static int32_t cache_from_bs(void)
{
  EFI_STATUS st;
  UINTN map_size;
  UINTN map_key;
  UINTN desc_size;
  UINT32 desc_ver;
  EFI_MEMORY_DESCRIPTOR *map;
  UINTN i;
  UINTN n;

  if (g_pm_efi_st == NULL || g_pm_efi_st->BootServices == NULL || !g_pm_efi_bs_alive) {
    return -1;
  }

  map_size = 0;
  st = g_pm_efi_st->BootServices->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
  if (st != EFI_BUFFER_TOO_SMALL || map_size == 0) {
    return -1;
  }
  map_size += 2u * desc_size;
  st = g_pm_efi_st->BootServices->AllocatePool(EfiLoaderData, map_size, (VOID **)&map);
  if (EFI_ERROR(st) || map == NULL) {
    return -1;
  }
  st = g_pm_efi_st->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_ver);
  if (EFI_ERROR(st)) {
    (void)g_pm_efi_st->BootServices->FreePool(map);
    return -1;
  }

  g_n_regions = 0;
  n = map_size / desc_size;
  for (i = 0; i < n && g_n_regions < MAX_REGIONS; i++) {
    EFI_MEMORY_DESCRIPTOR *d;
    uint64_t bytes;

    d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size);
    bytes = (uint64_t)d->NumberOfPages * 4096ull;
    if (bytes == 0) {
      continue;
    }
    g_regions[g_n_regions].addr = (uint64_t)d->PhysicalStart;
    g_regions[g_n_regions].len = bytes;
    g_regions[g_n_regions].type = map_efi_type(d->Type);
    g_regions[g_n_regions].reserved = 0;
    g_n_regions++;
  }
  (void)g_pm_efi_st->BootServices->FreePool(map);

  /* Image span via LoadedImage when possible. */
  g_image_base = 0;
  g_image_end = 0;
  if (g_pm_efi_image != NULL) {
    EFI_LOADED_IMAGE_PROTOCOL *li;

    st = g_pm_efi_st->BootServices->HandleProtocol(
        g_pm_efi_image, &g_loaded_image_guid, (VOID **)&li);
    if (!EFI_ERROR(st) && li != NULL && li->ImageBase != NULL) {
      g_image_base = (uintptr_t)li->ImageBase;
      g_image_end = g_image_base + (uintptr_t)li->ImageSize;
    }
  }
  g_cached = 1;
  return (g_n_regions > 0u) ? 0 : -1;
}

static int32_t efi_mem_map_get(pm_metal_boot_mem_region_t *out, uint32_t max, uint32_t *n_out)
{
  uint32_t i;
  uint32_t n;

  if (!g_cached) {
    if (cache_from_bs() != 0) {
      if (n_out != NULL) {
        *n_out = 0;
      }
      return -1;
    }
  }
  if (out == NULL || n_out == NULL) {
    return -1;
  }
  n = g_n_regions;
  if (n > max) {
    n = max;
  }
  for (i = 0; i < n; i++) {
    out[i] = g_regions[i];
  }
  *n_out = n;
  return 0;
}

static uintptr_t efi_image_base(void)
{
  if (!g_cached) {
    (void)cache_from_bs();
  }
  return g_image_base;
}

static uintptr_t efi_image_end(void)
{
  if (!g_cached) {
    (void)cache_from_bs();
  }
  return g_image_end;
}

/*
 * Claim the largest ConventionalMemory span (BIOS-like): own it as LoaderData
 * so BS cannot reuse it. A fixed AllocateAnyPages@32MiB left the rest as
 * scattered highmem chips in the boot tree.
 */
#define EFI_CLAIM_MIN_BYTES (2u * 1024u * 1024u)
#define EFI_PAGE 4096ull

static int32_t efi_claim_arena(uint8_t **base_out, size_t *bytes_out)
{
  EFI_STATUS st;
  UINTN map_size;
  UINTN map_key;
  UINTN desc_size;
  UINT32 desc_ver;
  EFI_MEMORY_DESCRIPTOR *map;
  UINTN i;
  UINTN n;
  uint64_t img_end;
  uint64_t best_addr = 0;
  uint64_t best_len = 0;
  EFI_PHYSICAL_ADDRESS phys;
  UINTN pages;

  if (base_out == NULL || bytes_out == NULL) {
    return -1;
  }
  if (g_pm_efi_st == NULL || g_pm_efi_st->BootServices == NULL || !g_pm_efi_bs_alive) {
    return -1;
  }

  if (!g_cached) {
    (void)cache_from_bs();
  }
  img_end = (uint64_t)g_image_end;
  if (img_end == 0u) {
    img_end = 0x100000ull;
  }
  img_end = (img_end + (EFI_PAGE - 1ull)) & ~(EFI_PAGE - 1ull);

  map_size = 0;
  st = g_pm_efi_st->BootServices->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
  if (st != EFI_BUFFER_TOO_SMALL || map_size == 0) {
    return -1;
  }
  map_size += 4u * desc_size;
  st = g_pm_efi_st->BootServices->AllocatePool(EfiLoaderData, map_size, (VOID **)&map);
  if (EFI_ERROR(st) || map == NULL) {
    return -1;
  }
  st = g_pm_efi_st->BootServices->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_ver);
  if (EFI_ERROR(st)) {
    (void)g_pm_efi_st->BootServices->FreePool(map);
    return -1;
  }

  n = map_size / desc_size;
  for (i = 0; i < n; i++) {
    EFI_MEMORY_DESCRIPTOR *d;
    uint64_t start;
    uint64_t end;
    uint64_t img_base;
    uint64_t left_len;
    uint64_t right_len;
    uint64_t cand_addr;
    uint64_t cand_len;

    d = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)map + i * desc_size);
    if (d->Type != EfiConventionalMemory || d->NumberOfPages == 0) {
      continue;
    }
    start = (uint64_t)d->PhysicalStart;
    end = start + (uint64_t)d->NumberOfPages * EFI_PAGE;
    img_base = (uint64_t)g_image_base;
    if (img_base == 0u) {
      img_base = img_end;
    }

    /*
     * Image often loads high under OVMF; the large Conventional span sits
     * below it. Only carve when this descriptor actually overlaps the image.
     */
    left_len = 0;
    right_len = 0;
    cand_addr = 0;
    cand_len = 0;
    if (img_end > img_base && start < img_end && end > img_base) {
      if (start < img_base) {
        uint64_t l0 = (start + (EFI_PAGE - 1ull)) & ~(EFI_PAGE - 1ull);
        uint64_t l1 = img_base & ~(EFI_PAGE - 1ull);
        if (l1 > l0) {
          left_len = l1 - l0;
          cand_addr = l0;
          cand_len = left_len;
        }
      }
      if (end > img_end) {
        uint64_t r0 = (img_end + (EFI_PAGE - 1ull)) & ~(EFI_PAGE - 1ull);
        uint64_t r1 = end & ~(EFI_PAGE - 1ull);
        if (r1 > r0) {
          right_len = r1 - r0;
          if (right_len > cand_len) {
            cand_addr = r0;
            cand_len = right_len;
          }
        }
      }
    } else {
      uint64_t a = (start + (EFI_PAGE - 1ull)) & ~(EFI_PAGE - 1ull);
      uint64_t b = end & ~(EFI_PAGE - 1ull);
      if (b > a) {
        cand_addr = a;
        cand_len = b - a;
      }
    }

    if (cand_len < (uint64_t)EFI_CLAIM_MIN_BYTES) {
      continue;
    }
    if (cand_len > best_len) {
      best_addr = cand_addr;
      best_len = cand_len;
    }
  }
  (void)g_pm_efi_st->BootServices->FreePool(map);

  if (best_len < (uint64_t)EFI_CLAIM_MIN_BYTES || best_addr == 0u) {
    return -1;
  }
  if (best_len > (uint64_t)SIZE_MAX) {
    best_len = (uint64_t)SIZE_MAX & ~(EFI_PAGE - 1ull);
  }

  pages = (UINTN)(best_len / EFI_PAGE);
  phys = (EFI_PHYSICAL_ADDRESS)best_addr;
  st = g_pm_efi_st->BootServices->AllocatePages(
      AllocateAddress, EfiLoaderData, pages, &phys);
  if (!EFI_ERROR(st) && phys == (EFI_PHYSICAL_ADDRESS)best_addr) {
    g_cached = 0;
    *base_out = (uint8_t *)(uintptr_t)phys;
    *bytes_out = (size_t)pages * (size_t)EFI_PAGE;
    return 0;
  }

  /* Prefer one large claim (BIOS-like). Shrink only via AllocateAnyPages. */
  while (pages >= (UINTN)(EFI_CLAIM_MIN_BYTES / (size_t)EFI_PAGE)) {
    phys = 0;
    st = g_pm_efi_st->BootServices->AllocatePages(
        AllocateAnyPages, EfiLoaderData, pages, &phys);
    if (!EFI_ERROR(st) && phys != 0) {
      g_cached = 0;
      *base_out = (uint8_t *)(uintptr_t)phys;
      *bytes_out = (size_t)pages * (size_t)EFI_PAGE;
      return 0;
    }
    pages /= 2u;
  }
  return -1;
}

static const pm_metal_boot_mem_map_ops_t g_ops = {
  .get = efi_mem_map_get,
  .image_base = efi_image_base,
  .image_end = efi_image_end,
  .claim_arena = efi_claim_arena,
};

const pm_metal_boot_mem_map_ops_t *pm_metal_boot_mem_map_ops(void)
{
  return &g_ops;
}
