/*
 * Orchestrator-only: load bootstrap blob from /sys/pm (one-time handoff).
 * Pair of metal/sys/hostinfo.h (engine publish).
 */
#ifndef PYMERGETIC_METAL_SYS_GUESTINFO_H_
#define PYMERGETIC_METAL_SYS_GUESTINFO_H_

#include <pymergetic/metal/sys/sys.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read and validate PM_METAL_SYS_BOOTSTRAP_PATH into out. */
int pm_metal_sys_guestinfo_load(pm_metal_sys_bootstrap_t *out);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SYS_GUESTINFO_H_ */
