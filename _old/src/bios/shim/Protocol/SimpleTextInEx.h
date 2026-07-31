#ifndef PM_BIOS_SHIM_PROTOCOL_SIMPLE_TEXT_IN_EX_H_
#define PM_BIOS_SHIM_PROTOCOL_SIMPLE_TEXT_IN_EX_H_

/* EDK2 <Uefi.h> already open — use the real MdePkg header next on -I. */
#ifdef __PI_UEFI_H__
#include_next <Protocol/SimpleTextInEx.h>
#else
#include "../PmBiosUefi.h"

#define EFI_SHIFT_STATE_VALID 0x80000000u
#define EFI_RIGHT_SHIFT_PRESSED 0x00000001u
#define EFI_LEFT_SHIFT_PRESSED 0x00000002u
#define EFI_RIGHT_CONTROL_PRESSED 0x00000004u
#define EFI_LEFT_CONTROL_PRESSED 0x00000008u
#define EFI_RIGHT_ALT_PRESSED 0x00000010u
#define EFI_LEFT_ALT_PRESSED 0x00000020u

#define SCAN_NULL 0x0000
#define SCAN_UP 0x0001
#define SCAN_DOWN 0x0002
#define SCAN_RIGHT 0x0003
#define SCAN_LEFT 0x0004
#define SCAN_HOME 0x0005
#define SCAN_END 0x0006
#define SCAN_INSERT 0x0007
#define SCAN_DELETE 0x0008
#define SCAN_PAGE_UP 0x0009
#define SCAN_PAGE_DOWN 0x000A
#define SCAN_F1 0x000B
#define SCAN_F2 0x000C
#define SCAN_F3 0x000D
#define SCAN_F4 0x000E
#define SCAN_F5 0x000F
#define SCAN_F6 0x0010
#define SCAN_F7 0x0011
#define SCAN_F8 0x0012
#define SCAN_F9 0x0013
#define SCAN_F10 0x0014
#define SCAN_ESC 0x0017

#define CHAR_NULL 0x0000
#define CHAR_BACKSPACE 0x0008
#define CHAR_TAB 0x0009
#define CHAR_LINEFEED 0x000A
#define CHAR_CARRIAGE_RETURN 0x000D

typedef struct {
  UINT32 KeyShiftState;
  UINT8 KeyToggleState;
} EFI_KEY_STATE;

typedef struct {
  EFI_INPUT_KEY Key;
  EFI_KEY_STATE KeyState;
} EFI_KEY_DATA;

typedef struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL;
struct _EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL {
  EFI_STATUS (*Reset)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This,
		      BOOLEAN ExtendedVerification);
  EFI_STATUS (*ReadKeyStrokeEx)(EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL *This,
				EFI_KEY_DATA *KeyData);
  VOID *WaitForKeyEx;
};

extern EFI_GUID gEfiSimpleTextInputExProtocolGuid;
#endif /* __PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_PROTOCOL_SIMPLE_TEXT_IN_EX_H_ */
