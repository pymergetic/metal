/** @file
  Forward to freestanding errno stub (see runtime/mem/host_stubs).
  Makes <errno.h> resolve when -I guest/wamr is active.
**/
#ifndef PM_METAL_GUEST_WAMR_ERRNO_H_
#define PM_METAL_GUEST_WAMR_ERRNO_H_

#include "../../runtime/mem/host_stubs/errno.h"

#endif /* PM_METAL_GUEST_WAMR_ERRNO_H_ */
