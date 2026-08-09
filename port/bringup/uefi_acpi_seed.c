#if METAL_BOARD_UEFI
#include <stdint.h>

#include <Uefi.h>

#include "pymergetic/metal/dev/acpi/__init__.h"
#include "uefi_mp.h"

/* EFI_ACPI_20_TABLE_GUID / ACPI_TABLE_GUID — slim edk_inc has no Guid/Acpi.h */
static const EFI_GUID k_acpi20 = {
    0x8868e871, 0xe4f1, 0x11d3, { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
};
static const EFI_GUID k_acpi10 = {
    0xeb9d2d31, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d }
};

static EFI_SYSTEM_TABLE *g_st;

EFI_SYSTEM_TABLE *pm_metal_uefi_system_table(void)
{
    return g_st;
}

static int guid_eq(const EFI_GUID *a, const EFI_GUID *b)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    UINTN i;

    for (i = 0; i < sizeof(EFI_GUID); i++) {
        if (pa[i] != pb[i]) {
            return 0;
        }
    }
    return 1;
}

void pm_metal_uefi_acpi_seed(EFI_SYSTEM_TABLE *st)
{
    UINTN i;
    uint64_t found = 0;

    g_st = st;
    if (st == NULL) {
        return;
    }
    for (i = 0; i < st->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *t = &st->ConfigurationTable[i];
        if (guid_eq(&t->VendorGuid, &k_acpi20)) {
            found = (uint64_t)(uintptr_t)t->VendorTable;
            break;
        }
        if (found == 0 && guid_eq(&t->VendorGuid, &k_acpi10)) {
            found = (uint64_t)(uintptr_t)t->VendorTable;
        }
    }
    if (found != 0) {
        pm_metal_dev_acpi_set_rsdp(found);
    }
}
#else
void pm_metal_uefi_acpi_seed(void *st)
{
    (void)st;
}
#endif
