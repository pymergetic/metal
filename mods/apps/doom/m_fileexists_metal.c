/*
 * M_FileExists — after async IWAD preload, the known path is "present" without
 * sync FS. Other paths still use sync size (config/saves omitted in v1).
 */
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include "metal_doom.h"

#include "pymergetic/metal/fs/fs.h"

typedef bool boolean;

boolean
__wrap_M_FileExists(char *filename)
{
	if (filename == NULL || filename[0] == '\0') {
		return false;
	}

	if (metal_doom_wad_ready()
	    && strcmp(filename, METAL_DOOM_IWAD) == 0) {
		return true;
	}

	return pm_metal_fs_size(filename) > 0;
}
