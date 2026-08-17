/* pymergetic.metal.boot.tree — floor print after pm_mod_boot_run. */
#ifndef PYMERGETIC_METAL_BOOT_TREE_TYPES_H
#define PYMERGETIC_METAL_BOOT_TREE_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t pm_metal_boot_tree_print(void);
void pm_metal_boot_motd(void);
void pm_metal_boot_shutdown(int reboot);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_TREE_TYPES_H */
