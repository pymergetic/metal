#ifndef PM_BIOS_SHIM_CPU_LIB_H_
#define PM_BIOS_SHIM_CPU_LIB_H_

/* EDK2 open — use MdePkg header from the next -I. */
#ifdef __PI_UEFI_H__
#include_next <Library/CpuLib.h>
#else

#include "../PmBiosUefi.h"

VOID CpuDeadLoop(VOID);
VOID DisableInterrupts(VOID);
VOID EnableInterrupts(VOID);

#endif /* !__PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_CPU_LIB_H_ */
