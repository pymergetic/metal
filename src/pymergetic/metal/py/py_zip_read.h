/**
 * @file Read-only accessor for a single member inside a STORED-only zip
 * (stdlib.zip). Used by py_port_stubs.c's mp_import_stat/mp_lexer_new_from_file
 * hooks so "import collections" resolves straight out of the archive —
 * no extraction to loose files, no dependency on MICROPY_VFS.
 */
#ifndef PYMERGETIC_METAL_PY_ZIP_READ_H_
#define PYMERGETIC_METAL_PY_ZIP_READ_H_

#include <stdint.h>

#define PY_ZIP_STAT_ERROR   (-1)
#define PY_ZIP_STAT_MISSING 0
#define PY_ZIP_STAT_FILE    1
#define PY_ZIP_STAT_DIR     2

/**
 * path is "<...>.zip/<member>" (any depth). Splits at the first ".zip/"
 * and dispatches to the zip reader; returns -1 if path has no ".zip/".
 * *out_size is set only on PY_ZIP_STAT_FILE.
 */
int32_t PyZipStatPath(const char *path, uint32_t *out_size);

/** Same split as PyZipStatPath; reads the member's raw (stored) bytes. */
uint32_t PyZipReadPath(const char *path, void *dest, uint32_t dest_len);

#endif /* PYMERGETIC_METAL_PY_ZIP_READ_H_ */
