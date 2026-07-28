/** @file Freestanding µPy port hooks (import_stat / open / lexer-from-file). */
#include <string.h>

#include <pymergetic/metal/fs/fs.h>

#include "py/builtin.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/obj.h"
#include "py/runtime.h"

#include "py_zip_read.h"

mp_import_stat_t mp_import_stat(const char *path)
{
  uint32_t           size;
  pm_metal_fs_stat_t st;

  if (path == NULL) {
    return MP_IMPORT_STAT_NO_EXIST;
  }

  if (strstr(path, ".zip/") != NULL) {
    /* Path crosses an archive boundary — real fs has nothing there either
     * way, so every outcome below (including error) is final, never a
     * fallthrough to the plain ESP stat. */
    switch (PyZipStatPath(path, &size)) {
    case PY_ZIP_STAT_FILE:
      return MP_IMPORT_STAT_FILE;
    case PY_ZIP_STAT_DIR:
      return MP_IMPORT_STAT_DIR;
    default:
      return MP_IMPORT_STAT_NO_EXIST;
    }
  }

  /* DIR matters: sys.path=/mods + package dirs (httpd/__init__.py). */
  if (pm_metal_fs_stat(path, &st) == 0) {
    if (st.type == PM_METAL_FS_TYPE_DIR) {
      return MP_IMPORT_STAT_DIR;
    }
    if (st.type == PM_METAL_FS_TYPE_FILE) {
      return MP_IMPORT_STAT_FILE;
    }
  }
  return MP_IMPORT_STAT_NO_EXIST;
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs)
{
  (void)n_args;
  (void)args;
  (void)kwargs;
  mp_raise_OSError(MP_ENOENT);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

/* No MICROPY_READER_VFS/POSIX — port supplies lexer-from-file via Metal FS
 * (or, for a ".zip/"-crossing path, py_zip_read.c's in-place archive reader). */
mp_lexer_t *mp_lexer_new_from_file(qstr filename)
{
  const char *path = qstr_str(filename);
  int32_t     is_zip;
  uint32_t    sz;
  uint32_t    n;
  char       *buf;

  is_zip = (strstr(path, ".zip/") != NULL);
  if (is_zip) {
    if (PyZipStatPath(path, &sz) != PY_ZIP_STAT_FILE || sz == 0 || sz > 256u * 1024u) {
      mp_raise_OSError(MP_ENOENT);
    }
  } else {
    sz = pm_metal_fs_size(path);
    if (sz == 0 || sz > 256u * 1024u) {
      mp_raise_OSError(MP_ENOENT);
    }
  }

  buf = m_new(char, sz + 1u);
  n   = is_zip ? PyZipReadPath(path, buf, sz) : pm_metal_fs_read(path, buf, sz);
  if (n == 0) {
    m_del(char, buf, sz + 1u);
    mp_raise_OSError(MP_ENOENT);
  }
  buf[n] = '\0';
  /* free_len = n+1 so reader close returns the buffer to the GC heap. */
  return mp_lexer_new_from_str_len(filename, buf, n, n + 1u);
}
