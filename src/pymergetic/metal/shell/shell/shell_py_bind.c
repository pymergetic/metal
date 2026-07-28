/** @file
  pmcmd.<name>(*args) — every registered shell command, real attributes on
  a short top-level `pmcmd` module (the one deliberate exception to the
  pymergetic.metal.* prefix — see docs/MICROPYTHON.md).

  Lazy attr over shell_cmd.c's *live* command table
  (pm_metal_shell_cmd_find — not a boot-time snapshot of
  `.pm_metal_shell_cmds.*`, and not a one-shot mp_store_attr walk). So a
  guest register_cmd() row and host cmds that appear after py init both
  show up on the next `pmcmd.<name>` load — see docs/DOC_IFACE_PLAN.md
  Part 0. No string-keyed dispatch at the call site: attribute access,
  then the same argc/argv + pm_metal_shell_out path as the console.
**/
#include <stdio.h>

#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/shell/shell_cmd.h>

#include "py/mpstate.h"
#include "py/obj.h"

typedef struct {
  mp_obj_base_t               base;
  const pm_metal_shell_cmd_t *cmd;
} pmcmd_proxy_obj_t;

#define PMCMD_ARG_BUF_LEN 80u

static mp_obj_t pmcmd_proxy_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args)
{
  pmcmd_proxy_obj_t *self = MP_OBJ_TO_PTR(self_in);
  char               arg_buf[PM_METAL_SHELL_ARGV_MAX][PMCMD_ARG_BUF_LEN];
  char              *argv[PM_METAL_SHELL_ARGV_MAX];
  int32_t            argc;
  size_t             i;

  if (n_kw != 0u) {
    pm_metal_py_raise_type_error("pmcmd: keyword args not supported");
  }
  if (n_args + 1u > PM_METAL_SHELL_ARGV_MAX) {
    pm_metal_py_raise_value_error("pmcmd: too many args");
  }

  snprintf(arg_buf[0], PMCMD_ARG_BUF_LEN, "%s", self->cmd->name);
  argv[0] = arg_buf[0];
  argc    = 1;

  for (i = 0; i < n_args; i++) {
    /* str(arg) — works for any Python object, same as what the console
     * shell would have seen typed as a token. */
    (void)pm_metal_py_obj_to_str((pm_metal_py_obj_t)args[i], arg_buf[argc], PMCMD_ARG_BUF_LEN);
    argv[argc] = arg_buf[argc];
    argc++;
  }

  self->cmd->fn(argc, argv);
  return mp_const_none;
}

/**
 * Load-only attr hook: __doc__ -> help/sig/body joined "\n\n" (base+face,
 * docs/DOC_IFACE_PLAN.md Part I — same "shell row" one-string-many-readers
 * home doc.lookup("shell", name) reads too, see util/doc.c). Any other
 * attribute (store, or a name that isn't __doc__) falls through to the
 * normal "not found" path — dest left untouched.
 */
static void pmcmd_proxy_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest)
{
  pmcmd_proxy_obj_t *self = MP_OBJ_TO_PTR(self_in);
  char               buf[300];
  size_t             off;

  if (dest[0] != MP_OBJ_NULL || attr != qstr_from_str("__doc__")) {
    return;
  }

  off = 0;
  if (self->cmd->help != NULL) {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s", self->cmd->help);
  }
  if (self->cmd->sig != NULL) {
    off += (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", off ? "\n\n" : "", self->cmd->sig);
  }
  if (self->cmd->body != NULL) {
    off +=
      (size_t)snprintf(buf + off, sizeof(buf) - off, "%s%s", off ? "\n\n" : "", self->cmd->body);
  }

  dest[0] = mp_obj_new_str(buf, off);
}

static MP_DEFINE_CONST_OBJ_TYPE(pmcmd_proxy_type,
                                MP_QSTR_function,
                                MP_TYPE_FLAG_NONE,
                                call,
                                pmcmd_proxy_call,
                                attr,
                                pmcmd_proxy_attr);

static mp_obj_t pmcmd_proxy_new(const pm_metal_shell_cmd_t *cmd)
{
  pmcmd_proxy_obj_t *proxy = m_new_obj(pmcmd_proxy_obj_t);
  proxy->base.type         = &pmcmd_proxy_type;
  proxy->cmd               = cmd;
  return MP_OBJ_FROM_PTR(proxy);
}

/**
 * Lazy load: look up the live table on every attribute access. Store/delete
 * and __dunder__ names fall through (AttributeError / normal module path).
 */
static void pmcmd_ns_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest)
{
  const char                 *name;
  const pm_metal_shell_cmd_t *cmd;

  (void)self_in;
  if (dest[0] != MP_OBJ_NULL) {
    return; /* store/delete unsupported */
  }

  name = qstr_str(attr);
  if (name == NULL || name[0] == '\0') {
    return;
  }
  /* Leave __name__/__dict__/... to MicroPython's normal "not found" path. */
  if (name[0] == '_' && name[1] == '_') {
    return;
  }

  cmd = pm_metal_shell_cmd_find(name);
  if (cmd == NULL || cmd->fn == NULL) {
    return;
  }

  dest[0] = pmcmd_proxy_new(cmd);
}

static MP_DEFINE_CONST_OBJ_TYPE(pmcmd_ns_type, MP_QSTR_object, MP_TYPE_FLAG_NONE, attr, pmcmd_ns_attr);

static const mp_obj_base_t g_pmcmd_ns_obj = { &pmcmd_ns_type };

/**
 * Registers the lazy pmcmd singleton into mp_loaded_modules_dict so
 * `import pmcmd` short-circuits before the filesystem (same trick as
 * pymergetic.metal.mod in mod_py_bind.c). Cheap to re-run per isolated
 * context — just one map slot, no per-cmd store_attr walk.
 */
void pm_metal_py_pmcmd_install(void)
{
  nlr_buf_t nlr;

  if (nlr_push(&nlr) == 0) {
    mp_map_t      *modules_map = &MP_STATE_VM(mp_loaded_modules_dict).map;
    mp_map_elem_t *el          = mp_map_lookup(modules_map, MP_OBJ_NEW_QSTR(qstr_from_str("pmcmd")),
                                      MP_MAP_LOOKUP_ADD_IF_NOT_FOUND);
    el->value                  = MP_OBJ_FROM_PTR(&g_pmcmd_ns_obj);
    nlr_pop();
  }
}
