#include "PmBiosUefi.h"
#include "Library/BaseMemoryLib.h"

VOID *
ZeroMem(VOID *Buffer, UINTN Length)
{
  UINT8 *p = (UINT8 *)Buffer;
  while (Length--)
    *p++ = 0;
  return Buffer;
}

VOID *
CopyMem(VOID *Destination, CONST VOID *Source, UINTN Length)
{
  UINT8 *d = (UINT8 *)Destination;
  CONST UINT8 *s = (CONST UINT8 *)Source;
  if (d == s || Length == 0)
    return Destination;
  if (d < s) {
    while (Length--)
      *d++ = *s++;
  } else {
    d += Length;
    s += Length;
    while (Length--)
      *--d = *--s;
  }
  return Destination;
}

VOID *
SetMem(VOID *Buffer, UINTN Length, UINT8 Value)
{
  UINT8 *p = (UINT8 *)Buffer;
  while (Length--)
    *p++ = Value;
  return Buffer;
}

INTN
CompareMem(CONST VOID *DestinationBuffer, CONST VOID *SourceBuffer, UINTN Length)
{
  CONST UINT8 *d = (CONST UINT8 *)DestinationBuffer;
  CONST UINT8 *s = (CONST UINT8 *)SourceBuffer;
  while (Length--) {
    if (*d != *s)
      return (INTN)*d - (INTN)*s;
    d++;
    s++;
  }
  return 0;
}
