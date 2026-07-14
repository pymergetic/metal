/*
 * Shell — guest-exec bridge. See guest_exec.h.
 *
 * The only file under shell/ that #includes wasm_export.h — kept
 * isolated here on purpose so shell.c, commands.c, and every file under
 * shell/commands/ stay entirely WAMR-free (shell.h's own header comment
 * promises "impl: common — zero OS dependency"; this file is the one
 * deliberate, documented exception, since its whole job is bridging
 * shell.c's own registry to WAMR's native-import mechanism).
 */
#include "pymergetic/metal/shell/guest_exec.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/console/console.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/shell/shell.h"
#include "wasm_export.h"

/* The native import itself. `name`/`arg` are already validated,
 * null-terminated `const char*`s pointing at translated native memory —
 * WAMR's own job, thanks to the "$" signature characters below (see
 * wasm_export.h's wasm_runtime_register_natives() doc comment), so no
 * manual wasm_runtime_addr_app_to_native()/validate_app_str_addr() is
 * needed here. */
static int32_t pm_metal_shell_guest_exec_native(wasm_exec_env_t exec_env, const char *name, const char *arg)
{
	wasm_module_inst_t inst = wasm_runtime_get_module_inst(exec_env);
	pm_metal_process_id_t pid;

	/* The pid run_ex() tagged this exact instance with (runtime.h's
	 * run_ex() custom_tag, set by process.c's spawn() — see there) —
	 * not the handle id. Pid-grained on purpose: the same handle can
	 * have several processes in flight at once (runtime/process.h),
	 * and it is specifically *this* execution's own output stream we
	 * need, not "some process or other currently running this
	 * handle". A custom_tag of 0 (never a real pid — process.h's own
	 * "0 == no process" convention) means this instance was run
	 * directly via runtime.h's run()/run_ex(), bypassing process.h
	 * entirely — guest_out below then correctly resolves to NULL. */
	pid.pid = (uint32_t)(uintptr_t)wasm_runtime_get_custom_data(inst);

	FILE *out = pm_metal_process_guest_out(pid);

	if (!out) {
		/* Expected, not just defensive, whenever this process was
		 * never spawn()ed with a guest_out at all (see the comment
		 * above) — not a "wrong name" failure, but NOT_FOUND is as
		 * good a sentinel as any other for "this never runs the
		 * command" and keeps the guest-visible contract to exactly
		 * two outcomes (see shell.h's PM_METAL_SHELL_EXEC_* pair). */
		return PM_METAL_SHELL_EXEC_NOT_FOUND;
	}

	/* Throwaway per-call context — not the persistent per-console cwd/
	 * env dispatch_line()'s own (human, or foreground .wasm command)
	 * callers get; see shell.h's doc comment on
	 * pm_metal_shell_guest_exec() for why that's an acceptable
	 * simplification for the currently guest_callable set. `sink` is
	 * a throwaway wrapper around `out` too, not a real registered
	 * console.h sink — every guest_callable command today only ever
	 * touches ->out (see shell/commands/), so kind/handle/label/
	 * fds left zeroed here are simply never read. */
	pm_metal_console_sink_t sink;
	pm_metal_shell_ctx_t ctx;

	memset(&sink, 0, sizeof(sink));
	sink.out = out;
	memset(&ctx, 0, sizeof(ctx));
	ctx.sink = &sink;
	snprintf(ctx.cwd, sizeof(ctx.cwd), "/");

	return pm_metal_shell_guest_exec(&ctx, name, arg);
}

static NativeSymbol g_pm_metal_shell_guest_exec_symbols[] = {
	{ "pm_metal_shell_exec", (void *)pm_metal_shell_guest_exec_native, "($$)i", NULL },
};

void pm_metal_shell_guest_exec_register(void)
{
	wasm_runtime_register_natives(
		"env", g_pm_metal_shell_guest_exec_symbols,
		sizeof(g_pm_metal_shell_guest_exec_symbols) / sizeof(g_pm_metal_shell_guest_exec_symbols[0]));
}
