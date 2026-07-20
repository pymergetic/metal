/*
 * M_FileExists via Metal ESP fs — WASI fopen preopen is still flaky for
 * absolute IWAD paths; D_FindWADByName only needs existence.
 */
#include <stddef.h>
#include <stdbool.h>

#include "pymergetic/metal/fs.h"

typedef bool boolean;

boolean
__wrap_M_FileExists(char *filename)
{
	if (filename == NULL || filename[0] == '\0') {
		return false;
	}

	return pm_metal_fs_size(filename) > 0;
}
