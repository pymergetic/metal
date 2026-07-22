#ifndef PM_BIOS_SHIM_SYNCHRONIZATION_LIB_H_
#define PM_BIOS_SHIM_SYNCHRONIZATION_LIB_H_

/* EDK2 open — use MdePkg header from the next -I. */
#ifdef __PI_UEFI_H__
#include_next <Library/SynchronizationLib.h>
#else

#include "../PmBiosUefi.h"

typedef volatile UINTN SPIN_LOCK;

VOID InitializeSpinLock(SPIN_LOCK *SpinLock);
VOID AcquireSpinLock(SPIN_LOCK *SpinLock);
VOID ReleaseSpinLock(SPIN_LOCK *SpinLock);
UINT32 InterlockedIncrement(volatile UINT32 *Value);
UINT32 InterlockedCompareExchange32(volatile UINT32 *Value, UINT32 CompareValue,
				    UINT32 ExchangeValue);

#endif /* !__PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_SYNCHRONIZATION_LIB_H_ */
