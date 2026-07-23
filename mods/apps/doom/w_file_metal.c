/*
 * WAD file class — serve async-preloaded IWAD buffer (no FS in OpenFile).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "metal_doom.h"

#include "../../../external/doomgeneric/doomgeneric/w_file.h"
#include "../../../external/doomgeneric/doomgeneric/z_zone.h"

#include "pymergetic/metal/shell/shell/shell.h"

typedef struct {
	wad_file_t wad;
	byte *data;
	int owned;
} metal_wad_file_t;

extern wad_file_class_t stdc_wad_file;

static uint8_t *s_wad_buf;
static uint32_t s_wad_len;

int
metal_doom_wad_install(uint8_t *buf, uint32_t len)
{
	if (buf == NULL || len == 0) {
		return -1;
	}

	s_wad_buf = buf;
	s_wad_len = len;
	return 0;
}

int
metal_doom_wad_ready(void)
{
	return (s_wad_buf != NULL && s_wad_len > 0) ? 1 : 0;
}

static wad_file_t *
W_Metal_OpenFile(char *path)
{
	metal_wad_file_t *result;

	(void)path;

	if (!metal_doom_wad_ready()) {
		pm_metal_shell_log("metal-doom: wad not preloaded");
		return NULL;
	}

	pm_metal_shell_log("metal-doom: wad open (memory)");

	result = Z_Malloc(sizeof(*result), PU_STATIC, 0);
	result->data = s_wad_buf;
	result->owned = 0; /* owned by metal_main preload */
	result->wad.file_class = &stdc_wad_file;
	result->wad.mapped = s_wad_buf;
	result->wad.length = s_wad_len;
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
	if (m->owned) {
		free(m->data);
	}

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
