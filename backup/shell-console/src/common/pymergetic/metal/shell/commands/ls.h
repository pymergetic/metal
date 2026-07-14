/*
 * Shell builtin — `ls [path]`. See shell/commands.h "ops struct" for how
 * this plugs into the registry — this is a plain function, callable
 * directly by anything that has a pm_metal_shell_ctx_t, not just through
 * dispatch_line()'s name-based lookup.
 */
#ifndef PYMERGETIC_METAL_SHELL_COMMANDS_LS_H_
#define PYMERGETIC_METAL_SHELL_COMMANDS_LS_H_

#include "pymergetic/metal/shell/shell.h"

/* impl: common — src/common/pymergetic/metal/shell/commands/ls.c */
int pm_metal_shell_cmd_ls(pm_metal_shell_ctx_t *ctx, int argc, char **argv);

#endif /* PYMERGETIC_METAL_SHELL_COMMANDS_LS_H_ */
