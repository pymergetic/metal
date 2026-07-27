/** @file
  pmcmd.<name>(*args) — every registered shell command, real attributes on
  a short top-level `pmcmd` module (the one deliberate exception to the
  pymergetic.metal.* prefix — see docs/MICROPYTHON.md).

  One proxy object per row of shell_cmd.c's own *live* command table
  (pm_metal_shell_cmd_count/at — not the `.pm_metal_shell_cmds.*` linker
  section directly, so a guest register_cmd() row shows up here too,
  see docs/DOC_IFACE_PLAN.md Part 0) — no new registry, no string-keyed
  dispatch at the call site (mod.func(...) attribute access, not
  pmcmd.run("name", ...)). A command invoked this
  way runs exactly like typing it at the console: synchronous C call in,
  same argc/argv shape, same pm_metal_shell_out() output; if the command
  itself starts a background task (e.g. `py`), that's the command's own
  business, same as from the console — see docs/MICROPYTHON.md's FACADE
  note and pymergetic.metal.process.* for observing completion.
**/
#include <stdio.h>

#include <pymergetic/metal/py/py.h>
#include <pymergetic/metal/py/py_obj.h>
#include <pymergetic/metal/shell/shell_cmd.h>

#include "py/obj.h"
#include "py/runtime.h"

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

/**
 * Installs one proxy per row of the *live* shell command table
 * (pm_metal_shell_cmd_count/at, shell_cmd.c) rather than walking the
 * `.pm_metal_shell_cmds.*` linker section directly — a guest
 * register_cmd() row only exists in that live table (see
 * docs/DOC_IFACE_PLAN.md Part 0 "shell enumeration ... uses the live
 * cmd table"), so walking the section here would silently miss it.
 */
void pm_metal_py_pmcmd_install(void)
{
  nlr_buf_t nlr;

  if (nlr_push(&nlr) == 0) {
    mp_obj_t pmcmd_mod = mp_obj_new_module(qstr_from_str("pmcmd"));
    uint32_t n         = pm_metal_shell_cmd_count();
    uint32_t i;

    for (i = 0; i < n; i++) {
      const pm_metal_shell_cmd_t *cmd = pm_metal_shell_cmd_at(i);
      pmcmd_proxy_obj_t          *proxy;

      if (cmd == NULL || cmd->name == NULL || cmd->fn == NULL) {
        continue;
      }

      proxy            = m_new_obj(pmcmd_proxy_obj_t);
      proxy->base.type = &pmcmd_proxy_type;
      proxy->cmd       = cmd;
      mp_store_attr(pmcmd_mod, qstr_from_str(cmd->name), MP_OBJ_FROM_PTR(proxy));
    }
    nlr_pop();
  }
}
