#ifndef PM_BIOS_SHIM_BASE_MEMORY_LIB_H_
#define PM_BIOS_SHIM_BASE_MEMORY_LIB_H_

/* EDK2 open — use MdePkg header from the next -I. */
#ifdef __PI_UEFI_H__
#include_next <Library/BaseMemoryLib.h>
#else

#include "../PmBiosUefi.h"

VOID *ZeroMem(VOID *Buffer, UINTN Length);
VOID *CopyMem(VOID *Destination, CONST VOID *Source, UINTN Length);
VOID *SetMem(VOID *Buffer, UINTN Length, UINT8 Value);
INTN CompareMem(CONST VOID *DestinationBuffer, CONST VOID *SourceBuffer,
		UINTN Length);

#endif /* !__PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_BASE_MEMORY_LIB_H_ */
