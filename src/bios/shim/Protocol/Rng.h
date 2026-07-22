#ifndef PM_BIOS_SHIM_PROTOCOL_RNG_H_
#define PM_BIOS_SHIM_PROTOCOL_RNG_H_

/* EDK2 <Uefi.h> already open — use the real MdePkg header next on -I. */
#ifdef __PI_UEFI_H__
#include_next <Protocol/Rng.h>
#else
#include "../PmBiosUefi.h"
extern EFI_GUID gEfiRngProtocolGuid;
#endif /* __PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_PROTOCOL_RNG_H_ */
