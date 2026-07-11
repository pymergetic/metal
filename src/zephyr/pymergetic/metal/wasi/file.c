/*
 * Zephyr WASI file shim — plat-private.
 */
#include "pymergetic/metal/wasi/file.h"

int pm_metal_wasi_file_init(const char *vfs_root)
{
	(void)vfs_root;
	return -1;
}
