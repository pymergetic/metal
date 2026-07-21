/*
 * WAD file class — load via Metal ESP fs into wasi-libc malloc buffer.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/*
 * Provide doomtype bits and skip vendor doomtype.h (pulls <strings.h>,
 * which host/IDE clang often lacks without wasi-sysroot).
 */
typedef bool boolean;
typedef uint8_t byte;
#define __DOOMTYPE__

#include "../../../external/doomgeneric/doomgeneric/w_file.h"
#include "../../../external/doomgeneric/doomgeneric/z_zone.h"

#include "pymergetic/metal/fs/fs.h"
#include "pymergetic/metal/shell/shell.h"

typedef struct {
	wad_file_t wad;
	byte *data;
} metal_wad_file_t;

extern wad_file_class_t stdc_wad_file;

static wad_file_t *
W_Metal_OpenFile(char *path)
{
	metal_wad_file_t *result;
	uint32_t len;
	uint32_t got;
	byte *buf;

	pm_metal_shell_log("metal-doom: wad open");
	len = pm_metal_fs_size(path);
	if (len == 0) {
		pm_metal_shell_log("metal-doom: wad size fail");
		return NULL;
	}

	buf = (byte *)malloc((size_t)len);
	if (buf == NULL) {
		pm_metal_shell_log("metal-doom: wad malloc fail");
		return NULL;
	}

	got = pm_metal_fs_read(path, (uint32_t)(uintptr_t)buf, len);
	if (got != len) {
		free(buf);
		pm_metal_shell_log("metal-doom: wad read fail");
		return NULL;
	}

	pm_metal_shell_log("metal-doom: wad open ok");

	result = Z_Malloc(sizeof(*result), PU_STATIC, 0);
	result->data = buf;
	result->wad.file_class = &stdc_wad_file;
	result->wad.mapped = buf;
	result->wad.length = len;
	return &result->wad;
}

static void
W_Metal_CloseFile(wad_file_t *wad)
{
	metal_wad_file_t *m;

	if (wad == NULL) {
		return;
	}

	m = (metal_wad_file_t *)wad;
	free(m->data);
	m->data = NULL;
	m->wad.mapped = NULL;
	Z_Free(wad);
}

static size_t
W_Metal_Read(wad_file_t *wad, unsigned int offset, void *buffer,
	     size_t buffer_len)
{
	byte *src;

	if (wad == NULL || wad->mapped == NULL || buffer == NULL) {
		return 0;
	}
	if (offset >= wad->length) {
		return 0;
	}
	if (offset + buffer_len > wad->length) {
		buffer_len = wad->length - offset;
	}

	src = wad->mapped + offset;
	memcpy(buffer, src, buffer_len);
	return buffer_len;
}

wad_file_class_t stdc_wad_file = {
	W_Metal_OpenFile,
	W_Metal_CloseFile,
	W_Metal_Read,
};
