#ifndef PM_BIOS_SHIM_PROTOCOL_MP_SERVICE_H_
#define PM_BIOS_SHIM_PROTOCOL_MP_SERVICE_H_

/* EDK2 <Uefi.h> already open — use the real MdePkg header next on -I. */
#ifdef __PI_UEFI_H__
#include_next <Protocol/MpService.h>
#else
#include "../PmBiosUefi.h"
typedef struct _EFI_MP_SERVICES_PROTOCOL EFI_MP_SERVICES_PROTOCOL;
extern EFI_GUID gEfiMpServiceProtocolGuid;
#endif /* __PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_PROTOCOL_MP_SERVICE_H_ */
