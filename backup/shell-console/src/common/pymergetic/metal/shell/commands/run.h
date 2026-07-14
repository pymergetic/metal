/*
 * Shell builtin — `run <id> [args...]`: spawns a new process (see
 * runtime/process.h) against an already-loaded handle. See
 * shell/commands.h "ops struct" for how this plugs into the registry —
 * this is a plain function, callable directly by anything that has a
 * pm_metal_shell_ctx_t, not just through dispatch_line()'s name-based
 * lookup.
 */
#ifndef PYMERGETIC_METAL_SHELL_COMMANDS_RUN_H_
#define PYMERGETIC_METAL_SHELL_COMMANDS_RUN_H_

#include "pymergetic/metal/shell/shell.h"

/* impl: common — src/common/pymergetic/metal/shell/commands/run.c */
int pm_metal_shell_cmd_run(pm_metal_shell_ctx_t *ctx, int argc, char **argv);

#endif /* PYMERGETIC_METAL_SHELL_COMMANDS_RUN_H_ */
