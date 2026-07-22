#ifndef PM_BIOS_SHIM_UEFI_RUNTIME_SERVICES_TABLE_LIB_H_
#define PM_BIOS_SHIM_UEFI_RUNTIME_SERVICES_TABLE_LIB_H_

#ifdef __PI_UEFI_H__
#include_next <Library/UefiRuntimeServicesTableLib.h>
#else
#include "../PmBiosUefi.h"

extern EFI_RUNTIME_SERVICES *gRT;
#endif

#endif /* PM_BIOS_SHIM_UEFI_RUNTIME_SERVICES_TABLE_LIB_H_ */
