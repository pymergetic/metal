/*
 * Port — zephyr bind. Stub — deferred with the rest of zephyr's shell
 * (see docs/RUNTIME.md "Bring-up plan" §5). A real implementation would
 * go through Zephyr's fs_opendir()/fs_readdir() (CONFIG_FILE_SYSTEM),
 * same reasoning as port/platform.h's read_file().
 */
#include "pymergetic/metal/port/dir.h"

int pm_metal_port_dir_list(const char *host_path, void (*visit)(const char *name, int is_dir, void *ctx),
			    void *ctx)
{
	(void)host_path;
	(void)visit;
	(void)ctx;
	return -1;
}
