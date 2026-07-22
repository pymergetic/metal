#include "PmBiosUefi.h"
#include "Library/BaseLib.h"
#include "Library/CpuLib.h"

UINTN
AsciiStrLen(CONST CHAR8 *String)
{
  UINTN n = 0;
  if (String == NULL)
    return 0;
  while (String[n] != '\0')
    n++;
  return n;
}

UINTN
AsciiStrnLenS(CONST CHAR8 *String, UINTN MaxSize)
{
  UINTN n = 0;
  if (String == NULL)
    return 0;
  while (n < MaxSize && String[n] != '\0')
    n++;
  return n;
}

RETURN_STATUS
AsciiStrnCpyS(CHAR8 *Destination, UINTN DestMax, CONST CHAR8 *Source,
	      UINTN Length)
{
  UINTN i;
  if (Destination == NULL || DestMax == 0)
    return 1;
  for (i = 0; i < DestMax - 1 && i < Length && Source != NULL && Source[i]; i++)
    Destination[i] = Source[i];
  Destination[i] = '\0';
  return RETURN_SUCCESS;
}

RETURN_STATUS
AsciiStrCpyS(CHAR8 *Destination, UINTN DestMax, CONST CHAR8 *Source)
{
  return AsciiStrnCpyS(Destination, DestMax, Source, DestMax);
}

INTN
AsciiStrCmp(CONST CHAR8 *FirstString, CONST CHAR8 *SecondString)
{
  while (*FirstString && *FirstString == *SecondString) {
    FirstString++;
    SecondString++;
  }
  return (INTN)(UINT8)*FirstString - (INTN)(UINT8)*SecondString;
}

INTN
AsciiStrnCmp(CONST CHAR8 *FirstString, CONST CHAR8 *SecondString, UINTN Length)
{
  while (Length && *FirstString && *FirstString == *SecondString) {
    FirstString++;
    SecondString++;
    Length--;
  }
  if (Length == 0)
    return 0;
  return (INTN)(UINT8)*FirstString - (INTN)(UINT8)*SecondString;
}

CHAR8 *
AsciiStrStr(CONST CHAR8 *String, CONST CHAR8 *SearchString)
{
  UINTN n;
  if (String == NULL || SearchString == NULL)
    return NULL;
  n = AsciiStrLen(SearchString);
  if (n == 0)
    return (CHAR8 *)String;
  for (; *String; String++) {
    if (AsciiStrnCmp(String, SearchString, n) == 0)
      return (CHAR8 *)String;
  }
  return NULL;
}

UINTN
AsciiStrDecimalToUintn(CONST CHAR8 *String)
{
  UINTN v = 0;
  if (String == NULL)
    return 0;
  while (*String >= '0' && *String <= '9') {
    v = v * 10u + (UINTN)(*String - '0');
    String++;
  }
  return v;
}

UINT64
AsciiStrHexToUint64(CONST CHAR8 *String)
{
  UINT64 v = 0;
  if (String == NULL)
    return 0;
  if (String[0] == '0' && (String[1] == 'x' || String[1] == 'X'))
    String += 2;
  for (;;) {
    CHAR8 c = *String++;
    UINT8 d;
    if (c >= '0' && c <= '9')
      d = (UINT8)(c - '0');
    else if (c >= 'a' && c <= 'f')
      d = (UINT8)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      d = (UINT8)(c - 'A' + 10);
    else
      break;
    v = (v << 4) | d;
  }
  return v;
}

VOID
MemoryFence(VOID)
{
  __asm__ __volatile__("mfence" ::: "memory");
}

UINT64
AsmReadTsc(VOID)
{
  UINT32 lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((UINT64)hi << 32) | lo;
}

VOID
CpuPause(VOID)
{
  __asm__ __volatile__("pause");
}

UINT16
SwapBytes16(UINT16 Value)
{
  return (UINT16)((Value << 8) | (Value >> 8));
}

UINT32
SwapBytes32(UINT32 Value)
{
  return ((Value & 0x000000ffu) << 24) | ((Value & 0x0000ff00u) << 8) |
	 ((Value & 0x00ff0000u) >> 8) | ((Value & 0xff000000u) >> 24);
}

UINT64
SwapBytes64(UINT64 Value)
{
  return ((UINT64)SwapBytes32((UINT32)Value) << 32) |
	 SwapBytes32((UINT32)(Value >> 32));
}

VOID
CpuDeadLoop(VOID)
{
  for (;;)
    CpuPause();
}

VOID
DisableInterrupts(VOID)
{
  __asm__ __volatile__("cli" ::: "memory");
}

VOID
EnableInterrupts(VOID)
{
  __asm__ __volatile__("sti" ::: "memory");
}

/* Freestanding abs for WAMR (ems_kfc) — not a full libc. */
int
abs(int x)
{
  return (x < 0) ? -x : x;
}
