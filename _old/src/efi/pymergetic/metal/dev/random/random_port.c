/** @file
  EFI random port — RNG protocol + GetTime wall clock.
**/

#include <pymergetic/metal/dev/random/random.h>
#include <Uefi.h>
#include <Protocol/Rng.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

STATIC EFI_RNG_PROTOCOL  *mRng;
STATIC INT32              mRngProbed;

STATIC VOID
MetalRandomProbe (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mRngProbed || gBS == NULL) {
    return;
  }

  mRngProbed = 1;
  Status     = gBS->LocateProtocol (
                      &gEfiRngProtocolGuid,
                      NULL,
                      (VOID **)&mRng
                      );
  if (EFI_ERROR (Status)) {
    mRng = NULL;
  }
}

uint32_t
pm_metal_random_fill_port (
  VOID      *dest,
  uint32_t   len
  )
{
  EFI_STATUS  Status;

  if (dest == NULL || len == 0) {
    return 0;
  }

  MetalRandomProbe ();
  if (mRng == NULL) {
    return 0;
  }

  Status = mRng->GetRNG (mRng, NULL, (UINTN)len, (UINT8 *)dest);
  if (EFI_ERROR (Status)) {
    return 0;
  }

  return len;
}

uint64_t
pm_metal_random_realtime_ms_port (
  VOID
  )
{
  EFI_TIME  T;
  UINT64    days;
  UINT64    sec;

  if (gRT == NULL) {
    return 0;
  }

  if (EFI_ERROR (gRT->GetTime (&T, NULL))) {
    return 0;
  }

  days = (UINT64)(T.Year - 1970u) * 365u
         + (UINT64)((T.Year - 1969u) / 4u)
         - (UINT64)((T.Year - 1901u) / 100u)
         + (UINT64)((T.Year - 1601u) / 400u);
  {
    STATIC CONST UINT16  mdays[] = {
      0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    UINTN  m;

    m = (T.Month >= 1 && T.Month <= 12) ? (UINTN)(T.Month - 1) : 0;
    days += mdays[m];
    if (T.Month > 2
        && ((T.Year % 4u) == 0)
        && (((T.Year % 100u) != 0) || ((T.Year % 400u) == 0)))
    {
      days += 1;
    }
  }

  days += (UINT64)(T.Day > 0 ? T.Day - 1 : 0);
  sec   = days * 86400ULL
          + (UINT64)T.Hour * 3600ULL
          + (UINT64)T.Minute * 60ULL
          + (UINT64)T.Second;
  return sec * 1000ULL + (UINT64)(T.Nanosecond / 1000000u);
}
