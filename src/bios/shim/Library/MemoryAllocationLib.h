#ifndef PM_BIOS_SHIM_MEMORY_ALLOCATION_LIB_H_
#define PM_BIOS_SHIM_MEMORY_ALLOCATION_LIB_H_

/* EDK2 open — use MdePkg header from the next -I. */
#ifdef __PI_UEFI_H__
#include_next <Library/MemoryAllocationLib.h>
#else

#include "../PmBiosUefi.h"

VOID *AllocatePool(UINTN AllocationSize);
VOID *AllocateZeroPool(UINTN AllocationSize);
VOID FreePool(VOID *Buffer);
VOID *AllocatePages(UINTN Pages);
VOID FreePages(VOID *Buffer, UINTN Pages);

void pm_bios_shim_set_heap(void *base, UINTN bytes);

#endif /* !__PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_MEMORY_ALLOCATION_LIB_H_ */
