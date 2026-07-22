/** @file
  BIOS random port — no EFI RNG; wall clock from CMOS RTC.
**/
#include <pymergetic/metal/dev/random/random.h>

#include <Uefi.h>
#include <Library/IoLib.h>

STATIC
UINT8
CmosRead (
  UINT8  reg
  )
{
  IoWrite8 (0x70, (UINT8)(reg | 0x80u));
  return IoRead8 (0x71);
}

STATIC
UINT8
CmosBcd (
  UINT8  v,
  INT32  binary
  )
{
  if (binary) {
    return v;
  }

  return (UINT8)(((v >> 4) * 10u) + (v & 0x0fu));
}

uint32_t
pm_metal_random_fill_port (
  VOID      *dest,
  uint32_t   len
  )
{
  (VOID)dest;
  (VOID)len;
  return 0;
}

uint64_t
pm_metal_random_realtime_ms_port (
  VOID
  )
{
  UINT8   st_b;
  INT32   binary;
  UINT8   sec;
  UINT8   min;
  UINT8   hour;
  UINT8   day;
  UINT8   month;
  UINT8   year;
  UINT16  y;
  UINT64  days;
  UINT64  secs;
  UINTN   m;
  STATIC CONST UINT16  mdays[] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
  };

  /* Wait briefly if update-in-progress. */
  {
    UINTN  i;

    for (i = 0; i < 1000u; i++) {
      if ((CmosRead (0x0A) & 0x80u) == 0) {
        break;
      }
    }
  }

  st_b   = CmosRead (0x0B);
  binary = (st_b & 0x04u) != 0 ? 1 : 0;
  sec    = CmosBcd (CmosRead (0x00), binary);
  min    = CmosBcd (CmosRead (0x02), binary);
  hour   = CmosRead (0x04);
  day    = CmosBcd (CmosRead (0x07), binary);
  month  = CmosBcd (CmosRead (0x08), binary);
  year   = CmosBcd (CmosRead (0x09), binary);

  /* 12-hour mode: bit 7 of hour is PM. */
  if ((st_b & 0x02u) == 0) {
    UINT8  pm;

    pm   = (UINT8)(hour & 0x80u);
    hour = CmosBcd ((UINT8)(hour & 0x7fu), binary);
    if (hour == 12u) {
      hour = 0;
    }

    if (pm != 0) {
      hour = (UINT8)(hour + 12u);
    }
  } else {
    hour = CmosBcd (hour, binary);
  }

  if (month < 1u || month > 12u || day < 1u || day > 31u
      || hour > 23u || min > 59u || sec > 59u)
  {
    return 0;
  }

  /* CMOS year is 00–99; assume 2000–2099. */
  y = (UINT16)(2000u + year);

  days = (UINT64)(y - 1970u) * 365u
         + (UINT64)((y - 1969u) / 4u)
         - (UINT64)((y - 1901u) / 100u)
         + (UINT64)((y - 1601u) / 400u);
  m     = (UINTN)(month - 1u);
  days += mdays[m];
  if (month > 2u
      && ((y % 4u) == 0)
      && (((y % 100u) != 0) || ((y % 400u) == 0)))
  {
    days += 1;
  }

  days += (UINT64)(day - 1u);
  secs  = days * 86400ULL
          + (UINT64)hour * 3600ULL
          + (UINT64)min * 60ULL
          + (UINT64)sec;
  return secs * 1000ULL;
}
