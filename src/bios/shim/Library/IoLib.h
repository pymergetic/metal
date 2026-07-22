#ifndef PM_BIOS_SHIM_IO_LIB_H_
#define PM_BIOS_SHIM_IO_LIB_H_

/* EDK2 open — use MdePkg header from the next -I. */
#ifdef __PI_UEFI_H__
#include_next <Library/IoLib.h>
#else

#include "../PmBiosUefi.h"

UINT8 IoRead8(UINTN Port);
UINT16 IoRead16(UINTN Port);
UINT32 IoRead32(UINTN Port);
VOID IoWrite8(UINTN Port, UINT8 Value);
VOID IoWrite16(UINTN Port, UINT16 Value);
VOID IoWrite32(UINTN Port, UINT32 Value);

#endif /* !__PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_IO_LIB_H_ */
