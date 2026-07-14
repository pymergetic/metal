/*
 * Shell — builtins ops struct + registration. See commands.h. Each
 * command's actual implementation lives in its own shell/commands/<name>.c
 * now (see there) — this file only assembles them into the ops struct and
 * drives pm_metal_shell_register_builtins() off of it. The handle table
 * itself (+ its init/shutdown) is shared infrastructure, not a command —
 * it lives one directory up, in shell/handles.c.
 */
#include "pymergetic/metal/shell/commands.h"

#include <stddef.h>

#include "pymergetic/metal/shell/commands/cd.h"
#include "pymergetic/metal/shell/commands/env.h"
#include "pymergetic/metal/shell/commands/exit.h"
#include "pymergetic/metal/shell/commands/export.h"
#include "pymergetic/metal/shell/commands/focus.h"
#include "pymergetic/metal/shell/commands/help.h"
#include "pymergetic/metal/shell/commands/load.h"
#include "pymergetic/metal/shell/commands/ls.h"
#include "pymergetic/metal/shell/commands/ps.h"
#include "pymergetic/metal/shell/commands/pwd.h"
#include "pymergetic/metal/shell/commands/quit.h"
#include "pymergetic/metal/shell/commands/run.h"
#include "pymergetic/metal/shell/commands/sleep.h"
#include "pymergetic/metal/shell/commands/uname.h"
#include "pymergetic/metal/shell/commands/unload.h"

const pm_metal_shell_builtins_ops_t *pm_metal_shell_builtins_ops(void)
{
	static const pm_metal_shell_builtins_ops_t ops = {
		.cd = pm_metal_shell_cmd_cd,
		.env = pm_metal_shell_cmd_env,
		.exit = pm_metal_shell_cmd_exit,
		.export = pm_metal_shell_cmd_export,
		.focus = pm_metal_shell_cmd_focus,
		.help = pm_metal_shell_cmd_help,
		.load = pm_metal_shell_cmd_load,
		.ls = pm_metal_shell_cmd_ls,
		.ps = pm_metal_shell_cmd_ps,
		.pwd = pm_metal_shell_cmd_pwd,
		.quit = pm_metal_shell_cmd_quit,
		.run = pm_metal_shell_cmd_run,
		.sleep = pm_metal_shell_cmd_sleep,
		.uname = pm_metal_shell_cmd_uname,
		.unload = pm_metal_shell_cmd_unload,
	};

	return &ops;
}

void pm_metal_shell_register_builtins(void)
{
	const pm_metal_shell_builtins_ops_t *ops = pm_metal_shell_builtins_ops();
	/* Trailing 1/0 is guest_callable (see shell.h's own doc comment on
	 * that field, and shell/guest_exec.c) — only commands with no
	 * shared/global side effect (nothing here touches another
	 * handle/process, the runtime's own lifecycle, or another
	 * console's state) opt in; everything else stays 0 (denied, but
	 * still listed — see ls.c/help.c, unaffected by this flag). */
	const pm_metal_shell_command_t builtins[] = {
		{ "cd", ops->cd, "cd [path] -- change the shell's working directory", 0 },
		{ "env", ops->env, "print this console's exported env vars", 1 },
		{ "exit", ops->exit, "alias for quit", 0 },
		{ "export", ops->export, "export KEY=VALUE -- set an env var for future run/wasm guests", 0 },
		{ "focus", ops->focus, "focus <id|kernel> -- switch the live pane", 0 },
		{ "help", ops->help, "list commands", 1 },
		{ "load", ops->load, "load <path> -- load a .wasm module", 0 },
		{ "ls", ops->ls, "ls [path] -- list a vfs_root directory", 1 },
		{ "ps", ops->ps, "list loaded handles and their processes", 1 },
		{ "pwd", ops->pwd, "print the shell's working directory", 1 },
		{ "quit", ops->quit, "shut down the runtime", 0 },
		{ "run", ops->run, "run <id> [args...] -- run a loaded handle", 0 },
		{ "sleep", ops->sleep, "sleep <seconds> -- pause this console for a while", 1 },
		{ "uname", ops->uname, "print the runtime's target/machine", 1 },
		{ "unload", ops->unload, "unload <id> -- unload a handle", 0 },
	};
	size_t i;

	for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
		pm_metal_shell_register(&builtins[i]);
	}
}
