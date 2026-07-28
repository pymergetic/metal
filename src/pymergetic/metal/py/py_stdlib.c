/** @file
  Loose stdlib on sys.path (/mods/py/stdlib). Prefer ESP/PXE-staged files;
  if missing, materialize once from iface pack py@metal.stdlib (baked into
  the signed kernel via embed-iface). No zip, no separate .sig.
**/
#include <stdint.h>
#include <stdio.h>

#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/util/iface.h>

#include "py/runtime.h"

#include "py_internal.h"

#define PM_METAL_PY_STDLIB_PROBE "/mods/py/stdlib/heapq.py"
#define PM_METAL_PY_STDLIB_PKG   "py@metal.stdlib"

void pm_metal_py_init_sys_path(void)
{
  mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/mods")));
  mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str(PM_METAL_PY_STDLIB_DIR)));
}

static int32_t StdlibPresent(void)
{
  return (pm_metal_fs_size(PM_METAL_PY_STDLIB_PROBE) > 0u) ? 1 : 0;
}

static int32_t WriteOne(const char *rel, const uint8_t *data, uint32_t len)
{
  char    path[PM_METAL_IFACE_PATH_MAX + 32u];
  int32_t n;

  if (rel == NULL || rel[0] == '\0' || data == NULL) {
    return -1;
  }
  n = (int32_t)snprintf(path, sizeof(path), "%s/%s", PM_METAL_PY_STDLIB_DIR, rel);
  if (n <= 0 || (uint32_t)n >= sizeof(path)) {
    return -1;
  }
  if (pm_metal_fs_write(path, data, len) != len) {
    return -1;
  }
  return 0;
}

void pm_metal_py_stdlib_ensure(void)
{
  int32_t        nfiles;
  int32_t        i;
  char           rel[PM_METAL_IFACE_PATH_MAX];
  const uint8_t *data;
  uint32_t       len;

  if (StdlibPresent() != 0) {
    return;
  }

  nfiles = pm_metal_iface_file_count(PM_METAL_PY_STDLIB_PKG);
  if (nfiles <= 0) {
    return;
  }

  for (i = 0; i < nfiles; i++) {
    if (pm_metal_iface_file_at(PM_METAL_PY_STDLIB_PKG, (uint32_t)i, rel, sizeof(rel)) != 0) {
      continue;
    }
    if (pm_metal_iface_file_open(PM_METAL_PY_STDLIB_PKG, rel, &data, &len) != 0) {
      continue;
    }
    (void)WriteOne(rel, data, len);
  }
}
