#ifndef PM_METAL_UEFI_MP_H_
#define PM_METAL_UEFI_MP_H_

#if METAL_BOARD_UEFI
#include <Uefi.h>

/* Minimal EFI_MP_SERVICES_PROTOCOL (slim edk_inc has no MpService.h). */
#define PM_EFI_MP_SERVICES_PROTOCOL_GUID \
    { 0x3fdda605, 0xa76e, 0x4f46, { 0xad, 0x29, 0x12, 0xf4, 0x53, 0x1b, 0x3d, 0x08 } }

typedef VOID(EFIAPI *PM_EFI_AP_PROCEDURE)(VOID *ProcedureArgument);

struct PM_EFI_MP_SERVICES_PROTOCOL;

typedef EFI_STATUS(EFIAPI *PM_MP_GET_NUMBER_OF_PROCESSORS)(
    struct PM_EFI_MP_SERVICES_PROTOCOL *This, UINTN *NumberOfProcessors,
    UINTN *NumberOfEnabledProcessors);

typedef EFI_STATUS(EFIAPI *PM_MP_STARTUP_THIS_AP)(
    struct PM_EFI_MP_SERVICES_PROTOCOL *This, PM_EFI_AP_PROCEDURE Procedure,
    UINTN ProcessorNumber, EFI_EVENT WaitEvent, UINTN TimeoutInMicroSeconds,
    VOID *ProcedureArgument, BOOLEAN *Finished);

typedef EFI_STATUS(EFIAPI *PM_MP_WHOAMI)(
    struct PM_EFI_MP_SERVICES_PROTOCOL *This, UINTN *ProcessorNumber);

typedef struct PM_EFI_MP_SERVICES_PROTOCOL {
    PM_MP_GET_NUMBER_OF_PROCESSORS GetNumberOfProcessors;
    VOID *GetProcessorInfo;
    VOID *StartupAllAPs;
    PM_MP_STARTUP_THIS_AP StartupThisAP;
    VOID *SwitchBSP;
    VOID *EnableDisableAP;
    PM_MP_WHOAMI WhoAmI;
} PM_EFI_MP_SERVICES_PROTOCOL;

EFI_SYSTEM_TABLE *pm_metal_uefi_system_table(void);
#endif

#endif
