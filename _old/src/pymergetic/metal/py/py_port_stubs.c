/** @file Freestanding µPy port hooks (import_stat / open / lexer-from-file). */
#include <pymergetic/metal/fs/fs.h>

#include "py/builtin.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/obj.h"
#include "py/runtime.h"

mp_import_stat_t mp_import_stat(const char *path)
{
  pm_metal_fs_stat_t st;

  if (path == NULL) {
    return MP_IMPORT_STAT_NO_EXIST;
  }

  /* DIR matters: sys.path=/mods + /mods/py/stdlib + package dirs. */
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

/* No MICROPY_READER_VFS/POSIX — port supplies lexer-from-file via Metal FS. */
mp_lexer_t *mp_lexer_new_from_file(qstr filename)
{
  const char *path = qstr_str(filename);
  uint32_t    sz;
  uint32_t    n;
  char       *buf;

  sz = pm_metal_fs_size(path);
  if (sz == 0 || sz > 256u * 1024u) {
    mp_raise_OSError(MP_ENOENT);
  }

  buf = m_new(char, sz + 1u);
  n   = pm_metal_fs_read(path, buf, sz);
  if (n == 0) {
    m_del(char, buf, sz + 1u);
    mp_raise_OSError(MP_ENOENT);
  }
  buf[n] = '\0';
  /* free_len = n+1 so reader close returns the buffer to the GC heap. */
  return mp_lexer_new_from_str_len(filename, buf, n, n + 1u);
}
