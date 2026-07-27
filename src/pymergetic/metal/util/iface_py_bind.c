/** @file
  pymergetic.metal.iface — Python face over util/iface.h's package +
  sym-table registry (docs/DOC_IFACE_PLAN.md Part II-D).
**/
#include <string.h>

#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/util/iface.h>

#include "py/obj.h"

static mp_obj_t PkgInfoDict(const pm_metal_iface_pkg_info_t *info)
{
  mp_obj_t d = pm_metal_py_dict_new(6);

  pm_metal_py_dict_set_str(d, "name", pm_metal_py_str_new(info->name));
  pm_metal_py_dict_set_str(
    d, "kind", pm_metal_py_str_new(info->kind == PM_METAL_IFACE_PKG_HEADERS ? "headers" : "sysroot"));
  pm_metal_py_dict_set_str(d, "version", pm_metal_py_str_new(info->version));
  pm_metal_py_dict_set_str(d, "abi_hash", pm_metal_py_str_new(info->abi_hash));
  pm_metal_py_dict_set_str(d, "nfiles", pm_metal_py_int_new((int64_t)info->nfiles));
  pm_metal_py_dict_set_str(d, "blob_len", pm_metal_py_int_new((int64_t)info->blob_len));
  return d;
}

/** {name: <pkg info dict>, ...} — every registered package. */
static mp_obj_t py_iface_info(void)
{
  int32_t  n = pm_metal_iface_pkg_count();
  int32_t  i;
  mp_obj_t d = pm_metal_py_dict_new((size_t)(n > 0 ? n : 1));

  for (i = 0; i < n; i++) {
    pm_metal_iface_pkg_info_t info;

    if (pm_metal_iface_pkg_at((uint32_t)i, &info) == 0) {
      pm_metal_py_dict_set_str(d, info.name, PkgInfoDict(&info));
    }
  }

  return d;
}
static MP_DEFINE_CONST_FUN_OBJ_0(py_iface_info_obj, py_iface_info);
PM_METAL_PY_BIND_DOC(g_py_bind_iface_info,
                    "pymergetic.metal.iface",
                    "info",
                    py_iface_info_obj,
                    PM_METAL_PY_SYNC,
                    "Every registered header pack, keyed by package name.",
                    "info() -> dict[str, dict]",
                    "Each value has the same shape as one list(pkg) row's own package "
                    "context: {name, kind, version, abi_hash, nfiles, blob_len}.");

/**
 * list() -> package names; list(pkg) -> file paths inside that package
 * (docs/DOC_IFACE_PLAN.md preview: `iface.list(); iface.list("metal.guest")`).
 */
static mp_obj_t py_iface_list(size_t n_args, const mp_obj_t *args)
{
  mp_obj_t out = pm_metal_py_list_new();

  if (n_args == 0) {
    int32_t n = pm_metal_iface_pkg_count();
    int32_t i;

    for (i = 0; i < n; i++) {
      pm_metal_iface_pkg_info_t info;

      if (pm_metal_iface_pkg_at((uint32_t)i, &info) == 0) {
        pm_metal_py_list_append(out, pm_metal_py_str_new(info.name));
      }
    }
    return out;
  }

  {
    const char *pkg = mp_obj_str_get_str(args[0]);
    int32_t     n   = pm_metal_iface_file_count(pkg);
    int32_t     i;

    if (n < 0) {
      pm_metal_py_raise_value_error("iface: unknown package");
    }

    for (i = 0; i < n; i++) {
      char path[PM_METAL_IFACE_PATH_MAX];

      if (pm_metal_iface_file_at(pkg, (uint32_t)i, path, sizeof(path)) == 0) {
        pm_metal_py_list_append(out, pm_metal_py_str_new(path));
      }
    }
  }

  return out;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_iface_list_obj, 0, 1, py_iface_list);
PM_METAL_PY_BIND_DOC(g_py_bind_iface_list,
                    "pymergetic.metal.iface",
                    "list",
                    py_iface_list_obj,
                    PM_METAL_PY_SYNC,
                    "Package names, or one package's own file paths.",
                    "list(pkg: str | None = None) -> list[str]",
                    "pkg is positional. Raises ValueError for an unknown package name.");

static mp_obj_t py_iface_read(mp_obj_t pkg_obj, mp_obj_t path_obj)
{
  const char    *pkg  = mp_obj_str_get_str(pkg_obj);
  const char    *path = mp_obj_str_get_str(path_obj);
  const uint8_t *data;
  uint32_t       len;

  if (pm_metal_iface_file_open(pkg, path, &data, &len) != 0) {
    pm_metal_py_raise_value_error("iface: file not found");
  }

  return pm_metal_py_bytes_new(data, len);
}
static MP_DEFINE_CONST_FUN_OBJ_2(py_iface_read_obj, py_iface_read);
PM_METAL_PY_BIND_DOC(g_py_bind_iface_read,
                    "pymergetic.metal.iface",
                    "read",
                    py_iface_read_obj,
                    PM_METAL_PY_SYNC,
                    "Whole file contents from a registered header pack.",
                    "read(pkg: str, path: str) -> bytes",
                    "path is exactly what list(pkg) returned (e.g. "
                    "'pymergetic/metal/fs/fs.h'). Raises ValueError if pkg/path is unknown.");

static mp_obj_t SymDict(const pm_metal_iface_sym_t *sym)
{
  mp_obj_t d = pm_metal_py_dict_new(5);

  pm_metal_py_dict_set_str(d, "module", pm_metal_py_str_new(sym->module));
  pm_metal_py_dict_set_str(d, "name", pm_metal_py_str_new(sym->name));
  pm_metal_py_dict_set_str(d, "sig", pm_metal_py_str_new(sym->sig));
  pm_metal_py_dict_set_str(d, "class_", pm_metal_py_int_new((int64_t)sym->class_));
  pm_metal_py_dict_set_str(d, "doc_key", pm_metal_py_str_new(sym->doc_key));
  return d;
}

/**
 * sym() -> every row; sym(module) -> rows in that wasi module;
 * sym(module, name) -> one dict or None (docs/DOC_IFACE_PLAN.md Part II /
 * Part III HTML /iface/sym listing).
 */
static mp_obj_t py_iface_sym(size_t n_args, const mp_obj_t *args)
{
  const char *module = NULL;
  const char *name   = NULL;

  if (n_args >= 1) {
    module = mp_obj_str_get_str(args[0]);
  }
  if (n_args >= 2) {
    name = mp_obj_str_get_str(args[1]);
  }

  if (module != NULL && name != NULL) {
    pm_metal_iface_sym_t sym;

    if (pm_metal_iface_sym_lookup(module, name, &sym) != 0) {
      return pm_metal_py_obj_none();
    }
    return SymDict(&sym);
  }

  {
    int32_t  n   = pm_metal_iface_sym_count();
    int32_t  i;
    mp_obj_t out = pm_metal_py_list_new();

    for (i = 0; i < n; i++) {
      pm_metal_iface_sym_t sym;

      if (pm_metal_iface_sym_at((uint32_t)i, &sym) != 0) {
        continue;
      }
      if (module != NULL && strcmp(sym.module, module) != 0) {
        continue;
      }
      pm_metal_py_list_append(out, SymDict(&sym));
    }
    return out;
  }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(py_iface_sym_obj, 0, 2, py_iface_sym);
PM_METAL_PY_BIND_DOC(g_py_bind_iface_sym,
                    "pymergetic.metal.iface",
                    "sym",
                    py_iface_sym_obj,
                    PM_METAL_PY_SYNC,
                    "List or look up scraped NativeSymbol rows.",
                    "sym(module: str | None = None, name: str | None = None) -> "
                    "list[dict] | dict | None",
                    "sym() lists every row; sym(module) filters to one wasi "
                    "module; sym(module, name) returns one "
                    "{module, name, sig, class_, doc_key} or None. doc_key is "
                    "'' unless scripts/iface_doc_keys.txt set one — pass a "
                    "non-empty doc_key to doc.lookup_key() for the readable text.");
