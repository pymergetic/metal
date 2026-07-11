/*
 * Zephyr WASI file shim — plat-private.
 * Virtual /sys/loader and vfs_root backing live here eventually.
 * See docs/RUNTIME.md · docs/WASI.md.
 */
#ifndef PYMERGETIC_METAL_WASI_FILE_H_
#define PYMERGETIC_METAL_WASI_FILE_H_

#include <stdint.h>

/* impl: zephyr */
int pm_metal_wasi_file_init(const char *vfs_root);

#endif /* PYMERGETIC_METAL_WASI_FILE_H_ */
