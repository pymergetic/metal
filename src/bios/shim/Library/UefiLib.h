#ifndef PM_BIOS_SHIM_UEFI_LIB_H_
#define PM_BIOS_SHIM_UEFI_LIB_H_

#ifdef __PI_UEFI_H__
#include_next <Library/UefiLib.h>
#else
/* BIOS: UefiLib callers expect Print() from PrintLib. */
#include <Library/PrintLib.h>
#endif

#endif /* PM_BIOS_SHIM_UEFI_LIB_H_ */
