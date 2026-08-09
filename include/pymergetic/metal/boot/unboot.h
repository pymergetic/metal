#ifndef PYMERGETIC_METAL_BOOT_UNBOOT_H_
#define PYMERGETIC_METAL_BOOT_UNBOOT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Reverse-boot work: shutting_down → quit all processes → seat resource hooks. */
int32_t pm_metal_boot_unboot(void);

int32_t pm_metal_boot_shutting_down(void);

/** Shim: unboot then seat halt/dead. */
int32_t pm_metal_boot_shutdown(void);

/** Shim: unboot then seat reset/revive. */
int32_t pm_metal_boot_reboot(void);

/** Optional seat hooks (set by port). NULL = best-effort defaults. */
typedef void (*pm_metal_boot_seat_power_fn)(void);
void pm_metal_boot_set_shutdown_hook(pm_metal_boot_seat_power_fn fn);
void pm_metal_boot_set_reboot_hook(pm_metal_boot_seat_power_fn fn);

/** 1 after shutdown() completed (browser dead face). Cleared by reboot revive. */
int32_t pm_metal_boot_is_dead(void);
void pm_metal_boot_clear_dead(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_UNBOOT_H_ */
