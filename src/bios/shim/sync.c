#include "PmBiosUefi.h"
#include "Library/SynchronizationLib.h"
#include "Library/BaseLib.h"

VOID
InitializeSpinLock(SPIN_LOCK *SpinLock)
{
  *SpinLock = 0;
}

VOID
AcquireSpinLock(SPIN_LOCK *SpinLock)
{
  while (__sync_lock_test_and_set(SpinLock, 1))
    CpuPause();
}

VOID
ReleaseSpinLock(SPIN_LOCK *SpinLock)
{
  __sync_lock_release(SpinLock);
}

UINT32
InterlockedIncrement(volatile UINT32 *Value)
{
  return __sync_add_and_fetch(Value, 1);
}

UINT32
InterlockedCompareExchange32(volatile UINT32 *Value, UINT32 CompareValue,
			     UINT32 ExchangeValue)
{
  return __sync_val_compare_and_swap(Value, CompareValue, ExchangeValue);
}
