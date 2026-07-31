#if defined(PM_METAL_BOOT_TARGET_EFI)
/* ok */
#elif defined(PM_METAL_BOOT_TARGET_BIOS)
#error "boot/efi/acpi_rsdp.c built with PM_METAL_BOOT_TARGET_BIOS"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif

#include <stdint.h>

#include <Uefi.h>
#include <Guid/Acpi.h>

#include "efi_ctx.h"

/* Linked from Rust acpi module. */
void pm_metal_dev_acpi_set_rsdp(uint64_t addr);

static int guid_eq(const EFI_GUID *a, const EFI_GUID *b)
{
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;
  uint32_t i;

  for (i = 0; i < sizeof(EFI_GUID); i++) {
    if (pa[i] != pb[i]) {
      return 0;
    }
  }
  return 1;
}

/* Seed RSDP from the EFI configuration table (before low-mem scan). */
void pm_metal_efi_acpi_seed(void)
{
  EFI_GUID acpi20 = EFI_ACPI_20_TABLE_GUID;
  EFI_GUID acpi10 = ACPI_10_TABLE_GUID;
  UINTN i;
  uint64_t found = 0;

  if (g_pm_efi_st == NULL) {
    return;
  }
  for (i = 0; i < g_pm_efi_st->NumberOfTableEntries; i++) {
    EFI_CONFIGURATION_TABLE *t = &g_pm_efi_st->ConfigurationTable[i];
    if (guid_eq(&t->VendorGuid, &acpi20)) {
      found = (uint64_t)(uintptr_t)t->VendorTable;
      break;
    }
    if (found == 0 && guid_eq(&t->VendorGuid, &acpi10)) {
      found = (uint64_t)(uintptr_t)t->VendorTable;
    }
  }
  if (found != 0) {
    pm_metal_dev_acpi_set_rsdp(found);
  }
}
