#ifndef PM_BIOS_SHIM_PROTOCOL_ABSOLUTE_POINTER_H_
#define PM_BIOS_SHIM_PROTOCOL_ABSOLUTE_POINTER_H_

/* EDK2 <Uefi.h> already open — use the real MdePkg header next on -I. */
#ifdef __PI_UEFI_H__
#include_next <Protocol/AbsolutePointer.h>
#else
#include "../PmBiosUefi.h"
extern EFI_GUID gEfiAbsolutePointerProtocolGuid;
#endif /* __PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_PROTOCOL_ABSOLUTE_POINTER_H_ */
