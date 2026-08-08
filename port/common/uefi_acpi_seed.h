#ifndef PM_METAL_UEFI_ACPI_SEED_H_
#define PM_METAL_UEFI_ACPI_SEED_H_

#if METAL_BOARD_UEFI
#include <Uefi.h>
void pm_metal_uefi_acpi_seed(EFI_SYSTEM_TABLE *st);
#else
void pm_metal_uefi_acpi_seed(void *st);
#endif

#endif
