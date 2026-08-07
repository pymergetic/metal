#include <Uefi.h>

#include "main_upy.h"
#include "console_smoke.h"
#include "floor_smoke.h"
#include "net_smoke.h"
#include "ip_smoke.h"
#include "draw_smoke.h"
#include "vt_smoke.h"
#include "tui_smoke.h"
#include "kbd_smoke.h"
#include "wamr_smoke.h"
#include "live_http.h"

void uart_init(void);
void uart_puts(const char *s);

#ifndef METAL_UPY_SMOKE
#define METAL_UPY_SMOKE 1
#endif

#ifndef METAL_LIVE
#define METAL_LIVE 0
#endif

EFI_STATUS EFIAPI UefiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    (void)ImageHandle;

    uart_init();
    uart_puts("metal X86_64_UEFI\n");

    if (pm_metal_console_smoke() != 0) {
        return EFI_DEVICE_ERROR;
    }

    if (pm_metal_floor_smoke() != 0) {
        return EFI_DEVICE_ERROR;
    }

    if (pm_metal_net_smoke() != 0) {
        return EFI_DEVICE_ERROR;
    }

    if (pm_metal_ip_smoke() != 0) {
        return EFI_DEVICE_ERROR;
    }

    if (pm_metal_draw_smoke() != 0) {
        return EFI_DEVICE_ERROR;
    }

    if (pm_metal_vt_smoke() != 0) {
        return EFI_DEVICE_ERROR;
    }

    if (pm_metal_tui_smoke() != 0) {
        return EFI_DEVICE_ERROR;
    }

    if (pm_metal_kbd_smoke() != 0) {
        return EFI_DEVICE_ERROR;
    }

#if defined(METAL_LINK_WAMR) && METAL_LINK_WAMR
    if (pm_metal_wamr_smoke() != 0) {
        return EFI_DEVICE_ERROR;
    }
#endif

    mp_metal_upy_run(METAL_UPY_SMOKE);

    uart_puts("ovmf ok\n");

#if METAL_LIVE
    pm_metal_live_http();
    return EFI_SUCCESS;
#else
    if (SystemTable != NULL && SystemTable->BootServices != NULL) {
        SystemTable->BootServices->Stall(200000);
    }
    return EFI_SUCCESS;
#endif
}
