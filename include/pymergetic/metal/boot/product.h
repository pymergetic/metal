#ifndef PYMERGETIC_METAL_BOOT_PRODUCT_H_
#define PYMERGETIC_METAL_BOOT_PRODUCT_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Live product boot (same on every seat).
 * Emits boot tree as stages complete → ready ok → rainbow MetalPython.
 * Returns 0 on success.
 */
int pm_metal_boot(void);

/**
 * After ready only (DOS autoexec). Optional CDN / helpers.
 * Returns 0 on success / no-op.
 */
int pm_metal_autoexec(void);

#ifdef __cplusplus
}
#endif

#endif
