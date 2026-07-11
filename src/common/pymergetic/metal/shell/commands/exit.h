/*
 * Shell builtin — `exit`: pure alias for `quit` (see quit.h) — same
 * shutdown, just a second, more Unix-conventional spelling of it. Kept
 * as its own function (a thin forward, see exit.c) rather than pointing
 * the registry/ops-struct straight at pm_metal_shell_cmd_quit, so every
 * registered name has exactly one matching pm_metal_shell_cmd_fn of its
 * own — same "one .h/.c pair per builtin" rule as every other command,
 * no special case for this one. See shell/commands.h "ops struct" for how
 * this plugs into the registry — this is a plain function, callable
 * directly by anything that has a pm_metal_shell_ctx_t, not just through
 * dispatch_line()'s name-based lookup.
 */
#ifndef PYMERGETIC_METAL_SHELL_COMMANDS_EXIT_H_
#define PYMERGETIC_METAL_SHELL_COMMANDS_EXIT_H_

#include "pymergetic/metal/shell/shell.h"

/* impl: common — src/common/pymergetic/metal/shell/commands/exit.c */
int pm_metal_shell_cmd_exit(pm_metal_shell_ctx_t *ctx, int argc, char **argv);

#endif /* PYMERGETIC_METAL_SHELL_COMMANDS_EXIT_H_ */
