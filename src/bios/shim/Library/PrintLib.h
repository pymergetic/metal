#ifndef PM_BIOS_SHIM_PRINT_LIB_H_
#define PM_BIOS_SHIM_PRINT_LIB_H_

/* EDK2 open — use MdePkg header from the next -I. */
#ifdef __PI_UEFI_H__
#include_next <Library/PrintLib.h>
#else

#include "../PmBiosUefi.h"

UINTN AsciiSPrint(CHAR8 *StartOfBuffer, UINTN BufferSize, CONST CHAR8 *FormatString,
		  ...);
UINTN AsciiVSPrint(CHAR8 *StartOfBuffer, UINTN BufferSize, CONST CHAR8 *FormatString,
		   VA_LIST Marker);
/* BIOS: Format is ignored; first vararg is CHAR8* line (EFI callers use L"%a\r\n"). */
UINTN Print(CONST VOID *Format, ...);

#endif /* !__PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_PRINT_LIB_H_ */
