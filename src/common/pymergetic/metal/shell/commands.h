/*
 * Shell — builtins ops struct + the loaded-handle table lifecycle. impl:
 * common — see shell.h's own header comment and docs/CONSOLE.md "Shell".
 * Everything reachable from here is what src/linux/main.c's cmd_*()/
 * g_handles used to be, unchanged in behavior, just: (a) registered
 * through shell.h instead of an if/else chain, (b) using
 * runtime/process.h instead of tracking a worker thread per handle by
 * hand, (c) with cd/pwd/ls/export/env added, and (d) one .h/.c pair per
 * command under shell/commands/ instead of one big commands.c — each
 * pm_metal_shell_cmd_*() is declared in its own commands/<name>.h and
 * thus directly `#include`able and callable by anything holding a
 * pm_metal_shell_ctx_t, not just reachable through the name-based
 * registry below. This header is only the compact, all-in-one-struct
 * view of that same set of functions (see pm_metal_shell_builtins_ops_t).
 */
#ifndef PYMERGETIC_METAL_SHELL_COMMANDS_H_
#define PYMERGETIC_METAL_SHELL_COMMANDS_H_

#include "pymergetic/metal/console/console.h"
#include "pymergetic/metal/shell/shell.h"

/* One field per registered name (alphabetical), see shell/commands/<name>.h
 * for each one's own doc comment — this struct does not repeat them.
 * `exit` is a thin forward to `quit`'s own implementation (see
 * commands/exit.h) rather than the same function pointer twice — every
 * field here has exactly one matching commands/<name>.{h,c} pair, no
 * special case for aliases. Mirrors the memory/ops.h ops-struct pattern:
 * a single compact, typed way to reach every builtin directly
 * (ops->load(ctx, argc, argv)), alongside (not instead of) the
 * name-based registry dispatch_line() already uses. */
typedef struct pm_metal_shell_builtins_ops {
	pm_metal_shell_cmd_fn cd;
	pm_metal_shell_cmd_fn env;
	pm_metal_shell_cmd_fn exit;
	pm_metal_shell_cmd_fn export;
	pm_metal_shell_cmd_fn focus;
	pm_metal_shell_cmd_fn help;
	pm_metal_shell_cmd_fn load;
	pm_metal_shell_cmd_fn ls;
	pm_metal_shell_cmd_fn ps;
	pm_metal_shell_cmd_fn pwd;
	pm_metal_shell_cmd_fn quit;
	pm_metal_shell_cmd_fn run;
	pm_metal_shell_cmd_fn unload;
} pm_metal_shell_builtins_ops_t;

/* impl: common — src/common/pymergetic/metal/shell/commands.c
 *
 * Returns a pointer to one process-lifetime-static instance — never
 * NULL, nothing to init first (each field is just another module's own
 * already-linked function, same as memory/ops.h's resolve() tables). */
const pm_metal_shell_builtins_ops_t *pm_metal_shell_builtins_ops(void);

/* impl: common — src/common/pymergetic/metal/shell/handles.c
 *
 * init(): zeroes the handle table this module owns (load/run/unload/ps/
 * focus all operate on it). `kernel_sink`: used only by `focus kernel`
 * (to switch the LOCAL viewport back to it regardless of which console
 * dispatched the command) — this module never opens/closes it, same "the
 * owner that opened it closes it" rule as everywhere else. `quit_cb`:
 * invoked (once) when the `quit`/`exit` builtin runs — the caller's own
 * "stop my pump loop" hook, kept out of this module entirely
 * (commands/quit.c has no idea what a pump loop even is on this target).
 * Call once, before pm_metal_shell_register_builtins(). */
void pm_metal_shell_handles_init(pm_metal_console_sink_t *kernel_sink, void (*quit_cb)(void));

/* impl: common — src/common/pymergetic/metal/shell/commands.c
 *
 * Registers every builtin (cd/env/exit/export/focus/help/load/ls/ps/
 * pwd/quit/run/unload) with the shell registry (see shell.h), sourcing
 * each one straight from pm_metal_shell_builtins_ops() — call once,
 * after pm_metal_shell_handles_init(). */
void pm_metal_shell_register_builtins(void);

/* impl: common — src/common/pymergetic/metal/shell/handles.c
 *
 * unload()s + unregisters + closes every still-active handle, then
 * viewport_shutdown()s PM_METAL_VIEWPORT_LOCAL — the same steps
 * src/linux/main.c's old shutdown_console_mode() used to do inline for
 * its g_handles table. Requires the caller to have already called
 * pm_metal_process_shutdown() (see runtime/process.h) — this module no
 * longer tracks per-handle workers itself, so unload() below would be
 * refused (handle busy) on any handle that still has an unjoined process
 * against it. Does NOT close kernel_sink itself or call
 * pm_metal_runtime_shutdown() — the caller opened the kernel sink and
 * still owns closing it; call process_shutdown(), then this, then close
 * the kernel sink, then runtime_shutdown() (same order the old
 * shutdown_console_mode() used, process_shutdown() newly first). Call
 * only after the pump loop has confirmed-stopped — never concurrently
 * with pump() (see the old g_quit_requested's doc comment for why; same
 * rule applies here). */
void pm_metal_shell_handles_shutdown(void);

#endif /* PYMERGETIC_METAL_SHELL_COMMANDS_H_ */
