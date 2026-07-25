/*
 * WAD file class — serve host-TLSF IWAD (no FS in OpenFile).
 *
 * Durable bytes stay on the Metal heap cookie. Guest never mmap's the WAD
 * into linear (`mapped == NULL`); W_Read copies out via mem_copy_out_at.
 */
#include <stdint.h>
#include <string.h>

#include "metal_doom.h"

#include "../../../external/doomgeneric/doomgeneric/w_file.h"
#include "../../../external/doomgeneric/doomgeneric/z_zone.h"

#include "pymergetic/metal/runtime/mem/mem.h"
#include "pymergetic/metal/fs/fs.h"
#include "pymergetic/metal/shell/shell/shell.h"

typedef struct {
  wad_file_t     wad;
  pm_metal_ptr_t host; /* TLSF cookie — not a linear pointer */
} metal_wad_file_t;

extern wad_file_class_t stdc_wad_file;

static pm_metal_ptr_t s_wad_mem;
static uint32_t       s_wad_len;

void metal_doom_wad_release(void)
{
  if (s_wad_mem != NULL) {
    pm_metal_mem_free(s_wad_mem);
    s_wad_mem = NULL;
  }
  s_wad_len = 0;
}

int metal_doom_wad_install(pm_metal_ptr_t host, uint32_t len)
{
  if (host == NULL || len == 0) {
    return -1;
  }

  metal_doom_wad_release();
  s_wad_mem = host;
  s_wad_len = len;
  return 0;
}

int metal_doom_wad_ready(void)
{
  return (s_wad_mem != NULL && s_wad_len > 0) ? 1 : 0;
}

static wad_file_t *W_Metal_OpenFile(char *path)
{
  metal_wad_file_t *result;

  (void)path;

  if (!metal_doom_wad_ready()) {
    pm_metal_shell_log("metal-doom: wad not preloaded");
    return NULL;
  }

  pm_metal_shell_log("metal-doom: wad open (host heap)");

  result                 = Z_Malloc(sizeof(*result), PU_STATIC, 0);
  result->host           = s_wad_mem;
  result->wad.file_class = &stdc_wad_file;
  /* NULL → W_CacheLumpNum uses W_Read (no guest dereference of cookie). */
  result->wad.mapped = NULL;
  result->wad.length = s_wad_len;
  return &result->wad;
}

static void W_Metal_CloseFile(wad_file_t *wad)
{
  if (wad == NULL) {
    return;
  }

  Z_Free(wad);
}

static size_t W_Metal_Read(wad_file_t *wad, unsigned int offset, void *buffer, size_t buffer_len)
{
  metal_wad_file_t *m;

  if (wad == NULL || buffer == NULL) {
    return 0;
  }

  m = (metal_wad_file_t *)wad;
  if (m->host == NULL) {
    return 0;
  }
  if (offset >= wad->length) {
    return 0;
  }
  if (offset + buffer_len > wad->length) {
    buffer_len = wad->length - offset;
  }

  if (pm_metal_mem_copy_out_at(
        m->host, (uint32_t)offset, PM_METAL_FS_IO_PTR(buffer), (uint32_t)buffer_len) != 0) {
    return 0;
  }

  return buffer_len;
}

wad_file_class_t stdc_wad_file = {
  W_Metal_OpenFile,
  W_Metal_CloseFile,
  W_Metal_Read,
};
