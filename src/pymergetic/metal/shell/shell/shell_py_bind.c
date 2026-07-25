/** @file
  pmcmd.<name>(*args) — every registered shell command, real attributes on
  a short top-level `pmcmd` module (the one deliberate exception to the
  pymergetic.metal.* prefix — see docs/MICROPYTHON.md).

  One proxy object per pm_metal_shell_cmd_t row from the same
  `.pm_metal_shell_cmds.*` linker section shell_cmd.c itself walks — no
  new registry, no string-keyed dispatch at the call site (mod.func(...)
  attribute access, not pmcmd.run("name", ...)). A command invoked this
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

extern const pm_metal_shell_cmd_table_t __pm_metal_shell_cmds_start[];
extern const pm_metal_shell_cmd_table_t __pm_metal_shell_cmds_end[];

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

static MP_DEFINE_CONST_OBJ_TYPE(
  pmcmd_proxy_type, MP_QSTR_function, MP_TYPE_FLAG_NONE, call, pmcmd_proxy_call);

void pm_metal_py_pmcmd_install(void)
{
  nlr_buf_t nlr;

  if (nlr_push(&nlr) == 0) {
    const pm_metal_shell_cmd_table_t *t;
    mp_obj_t                          pmcmd_mod = mp_obj_new_module(qstr_from_str("pmcmd"));

    for (t = __pm_metal_shell_cmds_start; t < __pm_metal_shell_cmds_end; t++) {
      uint32_t i;

      if (t->cmds == NULL || t->count == 0u) {
        continue;
      }

      for (i = 0; i < t->count; i++) {
        const pm_metal_shell_cmd_t *cmd = &t->cmds[i];
        pmcmd_proxy_obj_t          *proxy;

        if (cmd->name == NULL || cmd->fn == NULL) {
          continue;
        }

        proxy            = m_new_obj(pmcmd_proxy_obj_t);
        proxy->base.type = &pmcmd_proxy_type;
        proxy->cmd       = cmd;
        mp_store_attr(pmcmd_mod, qstr_from_str(cmd->name), MP_OBJ_FROM_PTR(proxy));
      }
    }
    nlr_pop();
  }
}
