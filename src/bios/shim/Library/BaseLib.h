#ifndef PM_BIOS_SHIM_BASE_LIB_H_
#define PM_BIOS_SHIM_BASE_LIB_H_

/* EDK2 open — use MdePkg header from the next -I. */
#ifdef __PI_UEFI_H__
#include_next <Library/BaseLib.h>
#else

#include "../PmBiosUefi.h"

#if defined(__i386__) && !defined(__x86_64__)
typedef struct {
  UINT32 Ebx;
  UINT32 Esp;
  UINT32 Ebp;
  UINT32 Esi;
  UINT32 Edi;
  UINT32 Eip;
} BASE_LIBRARY_JUMP_BUFFER;
#else
typedef struct {
  UINT64 Rbx;
  UINT64 Rsp;
  UINT64 Rbp;
  UINT64 R12;
  UINT64 R13;
  UINT64 R14;
  UINT64 R15;
  UINT64 Rip;
} BASE_LIBRARY_JUMP_BUFFER;
#endif

UINTN AsciiStrLen(CONST CHAR8 *String);
UINTN AsciiStrnLenS(CONST CHAR8 *String, UINTN MaxSize);
INTN AsciiStrCmp(CONST CHAR8 *FirstString, CONST CHAR8 *SecondString);
INTN AsciiStrnCmp(CONST CHAR8 *FirstString, CONST CHAR8 *SecondString, UINTN Length);
CHAR8 *AsciiStrStr(CONST CHAR8 *String, CONST CHAR8 *SearchString);
UINTN AsciiStrDecimalToUintn(CONST CHAR8 *String);
UINT64 AsciiStrHexToUint64(CONST CHAR8 *String);
RETURN_STATUS AsciiStrnCpyS(CHAR8 *Destination, UINTN DestMax,
			    CONST CHAR8 *Source, UINTN Length);
RETURN_STATUS AsciiStrCpyS(CHAR8 *Destination, UINTN DestMax,
			   CONST CHAR8 *Source);
VOID MemoryFence(VOID);
UINT64 AsmReadTsc(VOID);
VOID CpuPause(VOID);
UINT16 SwapBytes16(UINT16 Value);
UINT32 SwapBytes32(UINT32 Value);
UINT64 SwapBytes64(UINT64 Value);

UINTN SetJump(BASE_LIBRARY_JUMP_BUFFER *JumpBuffer);
VOID LongJump(BASE_LIBRARY_JUMP_BUFFER *JumpBuffer, UINTN Value);
VOID SwitchStack(VOID(EFIAPI *EntryPoint)(VOID *Context1, VOID *Context2),
		 VOID *Context1, VOID *Context2, VOID *NewStack);

#endif /* !__PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_BASE_LIB_H_ */
