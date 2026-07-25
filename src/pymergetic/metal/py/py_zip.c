/** @file Sample stdlib.zip path on sys.path (ESP / HTTP seed later). */
#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/shell/shell_cmd.h>

#include "py/runtime.h"
#include "py/objlist.h"

#include "py_internal.h"

#ifndef PM_METAL_PY_STDLIB_ZIP
#define PM_METAL_PY_STDLIB_ZIP "/mods/py/stdlib.zip"
#endif

static int g_zip_ok;

void pm_metal_py_zip_init_sys_path(void)
{
  mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/mods/py")));
  mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str(PM_METAL_PY_STDLIB_ZIP)));
}

int pm_metal_py_zip_ensure(void)
{
  if (g_zip_ok) {
    return 0;
  }
  if (pm_metal_fs_size(PM_METAL_PY_STDLIB_ZIP) > 0) {
    g_zip_ok = 1;
    return 0;
  }
  pm_metal_shell_out("py: stdlib.zip miss (optional for hello)");
  return -1;
}
