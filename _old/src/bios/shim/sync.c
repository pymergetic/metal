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

UINT64
InterlockedCompareExchange64(volatile UINT64 *Value, UINT64 CompareValue,
			     UINT64 ExchangeValue)
{
  /* i386's default UINT64 alignment is only 4, so clang can't derive the
   * real 8-byte natural alignment cmpxchg8b atomicity needs from Value's
   * type alone. Every caller now places its UINT64 on an 8-byte boundary
   * (aligned(8) at the declaration) to make that guarantee true; assert
   * it here too so the __sync builtin emits the plain atomic form instead
   * of a warning. */
  typedef volatile UINT64 __attribute__((aligned(8))) Aligned8Uint64;

  return __sync_val_compare_and_swap((Aligned8Uint64 *)Value, CompareValue,
				      ExchangeValue);
}
