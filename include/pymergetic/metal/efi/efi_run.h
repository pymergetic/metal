/*
 * Post-ExitBootServices owned entry (host-only).
 */
#ifndef PYMERGETIC_METAL_EFI_RUN_H_
#define PYMERGETIC_METAL_EFI_RUN_H_

#include <Uefi.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 1 after successful ExitBootServices. */
int pm_metal_efi_owned(void);

/**
 * Snapshot console + ExitBootServices, then enter owned runloop.
 * Does not return on success (resets on shell exit).
 */
EFI_STATUS
pm_metal_efi_exit_boot_and_run (
  EFI_HANDLE        ImageHandle,
  EFI_SYSTEM_TABLE  *SystemTable
  );

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_EFI_RUN_H_ */
