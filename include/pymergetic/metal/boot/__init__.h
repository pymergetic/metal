#ifndef PYMERGETIC_METAL_BOOT_H_
#define PYMERGETIC_METAL_BOOT_H_

/*
 * pymergetic.metal.boot — thin face over boot.tree UX helpers.
 * Frozen boot/__init__.py is comment-only; muscle lives in boot.tree C.
 */

#ifdef __cplusplus
extern "C" {
#endif

void pm_metal_boot_banner(const char *version, const char *cpu);
void pm_metal_boot_emit(const char *line);
void pm_metal_boot_tree_ready_ok(void);
int pm_metal_boot_tree_print(void);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_H_ */
