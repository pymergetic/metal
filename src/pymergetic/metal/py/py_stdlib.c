/** @file
  Loose Python trees on sys.path under /mods.
  Prefer ESP/PXE-staged files; if missing, materialize once from baked
  iface packs (embed-iface). No zip, no separate .sig.
**/
#include <stdint.h>
#include <stdio.h>

#include <pymergetic/metal/fs/fs.h>
#include <pymergetic/metal/util/iface.h>

#include "py/runtime.h"

#include "py_internal.h"

#define PM_METAL_PY_STDLIB_PROBE "/mods/py/stdlib/heapq.py"
#define PM_METAL_PY_STDLIB_PKG   "py@metal.stdlib"
#define PM_METAL_PY_GUEST_PROBE  "/mods/httpd/__init__.py"
#define PM_METAL_PY_GUEST_PKG    "py@metal.guest"

void pm_metal_py_init_sys_path(void)
{
  mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/mods")));
  mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str(PM_METAL_PY_STDLIB_DIR)));
}

static int32_t PathPresent(const char *path)
{
  return (pm_metal_fs_size(path) > 0u) ? 1 : 0;
}

static int32_t WriteUnder(const char *root, const char *rel, const uint8_t *data, uint32_t len)
{
  char    path[PM_METAL_IFACE_PATH_MAX + 32u];
  int32_t n;

  if (root == NULL || rel == NULL || rel[0] == '\0' || data == NULL) {
    return -1;
  }
  n = (int32_t)snprintf(path, sizeof(path), "%s/%s", root, rel);
  if (n <= 0 || (uint32_t)n >= sizeof(path)) {
    return -1;
  }
  if (pm_metal_fs_write(path, data, len) != len) {
    return -1;
  }
  return 0;
}

static void MaterializePkg(const char *pkg, const char *dest_root)
{
  int32_t        nfiles;
  int32_t        i;
  char           rel[PM_METAL_IFACE_PATH_MAX];
  const uint8_t *data;
  uint32_t       len;

  nfiles = pm_metal_iface_file_count(pkg);
  if (nfiles <= 0) {
    return;
  }

  for (i = 0; i < nfiles; i++) {
    if (pm_metal_iface_file_at(pkg, (uint32_t)i, rel, sizeof(rel)) != 0) {
      continue;
    }
    if (pm_metal_iface_file_open(pkg, rel, &data, &len) != 0) {
      continue;
    }
    (void)WriteUnder(dest_root, rel, data, len);
  }
}

void pm_metal_py_stdlib_ensure(void)
{
  if (PathPresent(PM_METAL_PY_STDLIB_PROBE) != 0) {
    return;
  }
  MaterializePkg(PM_METAL_PY_STDLIB_PKG, PM_METAL_PY_STDLIB_DIR);
}

void pm_metal_py_guest_ensure(void)
{
  /* httpd/api/microdot/utemplate/templates — py@metal.guest layout under /mods. */
  if (PathPresent(PM_METAL_PY_GUEST_PROBE) != 0) {
    return;
  }
  MaterializePkg(PM_METAL_PY_GUEST_PKG, "/mods");
}
