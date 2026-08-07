#include <Uefi.h>

#include "main_upy.h"
#include "floor_smoke.h"

void uart_init(void);
void uart_puts(const char *s);

#ifndef METAL_UPY_SMOKE
#define METAL_UPY_SMOKE 1
#endif

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    (void)ImageHandle;

    uart_init();
    uart_puts("metal X86_64_UEFI\n");

    if (pm_metal_floor_smoke() != 0) {
        return EFI_DEVICE_ERROR;
    }

    mp_metal_upy_run(METAL_UPY_SMOKE);

    uart_puts("ovmf ok\n");

    if (SystemTable != NULL && SystemTable->BootServices != NULL) {
        SystemTable->BootServices->Stall(200000);
    }
    return EFI_SUCCESS;
}
