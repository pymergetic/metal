/*
 * Shared EFI Boot Services context for platform ports (hidden tree).
 * Set once from UefiMain before bringup.
 */
#ifndef PYMERGETIC_METAL_BOOT_EFI_CTX_H_
#define PYMERGETIC_METAL_BOOT_EFI_CTX_H_

#include <Uefi.h>

#ifdef __cplusplus
extern "C" {
#endif

extern EFI_HANDLE g_pm_efi_image;
extern EFI_SYSTEM_TABLE *g_pm_efi_st;
/* 1 while Boot Services are usable; 0 after ExitBootServices. */
extern int g_pm_efi_bs_alive;

void pm_metal_efi_ctx_set(EFI_HANDLE image, EFI_SYSTEM_TABLE *st);
void pm_metal_efi_ctx_bs_dead(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_EFI_CTX_H_ */
