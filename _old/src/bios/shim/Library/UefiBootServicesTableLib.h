#ifndef PM_BIOS_SHIM_UEFI_BOOT_SERVICES_TABLE_LIB_H_
#define PM_BIOS_SHIM_UEFI_BOOT_SERVICES_TABLE_LIB_H_

#ifdef __PI_UEFI_H__
#include_next <Library/UefiBootServicesTableLib.h>
#else
#include "../PmBiosUefi.h"

/* Same exports as MdePkg Library/UefiBootServicesTableLib.h */
extern EFI_HANDLE gImageHandle;
extern EFI_SYSTEM_TABLE_MIN *gST;
extern EFI_BOOT_SERVICES *gBS;
#endif

#endif /* PM_BIOS_SHIM_UEFI_BOOT_SERVICES_TABLE_LIB_H_ */
