#ifndef PM_BIOS_SHIM_PROTOCOL_SIMPLE_FILE_SYSTEM_H_
#define PM_BIOS_SHIM_PROTOCOL_SIMPLE_FILE_SYSTEM_H_

/* EDK2 <Uefi.h> already open — use the real MdePkg header next on -I. */
#ifdef __PI_UEFI_H__
#include_next <Protocol/SimpleFileSystem.h>
#else
#include "../PmBiosUefi.h"
extern EFI_GUID gEfiSimpleFileSystemProtocolGuid;
extern EFI_GUID gEfiFileInfoGuid;
#endif /* __PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_PROTOCOL_SIMPLE_FILE_SYSTEM_H_ */
