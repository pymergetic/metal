/*
 * Bootstrap exchange: engine publishes once to /sys/pm, orchestrator reads once at boot.
 */
#ifndef PYMERGETIC_METAL_SYS_H_
#define PYMERGETIC_METAL_SYS_H_

#include <pymergetic/metal/util/wiretag.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Guest path — preopened by engine (dir mapped to /sys/pm). */
#define PM_METAL_SYS_BOOTSTRAP_PATH "/sys/pm/bootstrap"

#define PM_METAL_SYS_MAGIC   PM_METAL_UTIL_FOURCC_LE('P', 'M', 'S', 'Y')
#define PM_METAL_SYS_VERSION 1u
#define PM_METAL_SYS_TAG     PM_METAL_UTIL_WIRETAG(PM_METAL_SYS_MAGIC, PM_METAL_SYS_VERSION)
#define PM_METAL_SYS_BLOB_SIZE 32u

/* On-wire fields are LE — see pymergetic/metal/util/endian.h. */
typedef struct pm_metal_sys_bootstrap {
	pm_metal_util_wiretag_t tag;
	uint32_t size;
	uint32_t _reserved;
	uint64_t machine_ram;
	uint64_t arena_budget;
	uint64_t link_used;
} pm_metal_sys_bootstrap_t;

/* Orchestrator — guestinfo_load once, or sys_init() to cache getters. */
int pm_metal_sys_init(void);
int pm_metal_sys_ready(void);
uint64_t pm_metal_sys_machine_ram(void);
uint64_t pm_metal_sys_arena_budget(void);
uint64_t pm_metal_sys_link_used(void);

/* Engine — encode + hostinfo_publish(). Guest — guestinfo_load() or sys_init(). */
int pm_metal_sys_bootstrap_encode(pm_metal_sys_bootstrap_t *out);
int pm_metal_sys_bootstrap_validate(const pm_metal_sys_bootstrap_t *blob);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SYS_H_ */
