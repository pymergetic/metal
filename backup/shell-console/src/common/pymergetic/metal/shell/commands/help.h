/*
 * Shell builtin — `help`: lists every registered command (native + the
 * note about wasm overrides). See shell/commands.h "ops struct" for how
 * this plugs into the registry, and shell.h for pm_metal_shell_cmd_fn's
 * shape — this is a plain function, callable directly by anything that
 * has a pm_metal_shell_ctx_t, not just through dispatch_line()'s
 * name-based lookup.
 */
#ifndef PYMERGETIC_METAL_SHELL_COMMANDS_HELP_H_
#define PYMERGETIC_METAL_SHELL_COMMANDS_HELP_H_

#include "pymergetic/metal/shell/shell.h"

/* impl: common — src/common/pymergetic/metal/shell/commands/help.c */
int pm_metal_shell_cmd_help(pm_metal_shell_ctx_t *ctx, int argc, char **argv);

#endif /* PYMERGETIC_METAL_SHELL_COMMANDS_HELP_H_ */
