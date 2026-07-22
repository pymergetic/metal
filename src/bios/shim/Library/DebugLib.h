#ifndef PM_BIOS_SHIM_DEBUG_LIB_H_
#define PM_BIOS_SHIM_DEBUG_LIB_H_

/* EDK2 open — use MdePkg header from the next -I. */
#ifdef __PI_UEFI_H__
#include_next <Library/DebugLib.h>
#else

#include <Library/BaseLib.h>

#define ASSERT(Expression)                                                     \
  do {                                                                         \
    if (!(Expression)) {                                                       \
      for (;;) {                                                               \
	CpuPause();                                                            \
      }                                                                        \
    }                                                                          \
  } while (0)

#endif /* !__PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_DEBUG_LIB_H_ */
