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
	const pm_metal_shell_command_t builtins[] = {
		{ "cd", ops->cd, "cd [path] -- change the shell's working directory" },
		{ "env", ops->env, "print this console's exported env vars" },
		{ "exit", ops->exit, "alias for quit" },
		{ "export", ops->export, "export KEY=VALUE -- set an env var for future run/wasm guests" },
		{ "focus", ops->focus, "focus <id|kernel> -- switch the live pane" },
		{ "help", ops->help, "list commands" },
		{ "load", ops->load, "load <path> -- load a .wasm module" },
		{ "ls", ops->ls, "ls [path] -- list a vfs_root directory" },
		{ "ps", ops->ps, "list loaded handles and their processes" },
		{ "pwd", ops->pwd, "print the shell's working directory" },
		{ "quit", ops->quit, "shut down the runtime" },
		{ "run", ops->run, "run <id> [args...] -- run a loaded handle" },
		{ "sleep", ops->sleep, "sleep <seconds> -- pause this console for a while" },
		{ "uname", ops->uname, "print the runtime's target/machine" },
		{ "unload", ops->unload, "unload <id> -- unload a handle" },
	};
	size_t i;

	for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
		pm_metal_shell_register(&builtins[i]);
	}
}
