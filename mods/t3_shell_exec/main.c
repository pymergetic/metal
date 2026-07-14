/*
 * T3 — exercises the guest-exec bridge (pymergetic/metal/shell/
 * guest_exec.h): a WASM guest calling pm_metal_shell_exec(name, arg)
 * directly, like a syscall, not through any file I/O (WASI has no
 * "exec another program" concept). Runs the same under console mode and
 * scripted mode alike (see docs/CONSOLE.md "Guest-callable commands"
 * "Scope") — used by both scripts/verify-linux.sh (scripted) and
 * scripts/verify-linux-console.sh (console). Three calls, one line of
 * output each so a shell script can assert on all three documented
 * outcomes:
 *   - "pwd" is guest_callable -> actually runs, returns its own exit
 *     code (0), and its own output ("/") lands wherever this process's
 *     own stdout already goes (a console pane in console mode, real
 *     host stdout in scripted mode), interleaved with this mod's own
 *     stdout either way.
 *   - "quit" exists but is not guest_callable -> refused as DENIED
 *     (-1001), never actually invoked (the runtime keeps running).
 *   - "no_such_command" isn't registered at all -> NOT_FOUND (-1000),
 *     distinct from DENIED so a guest can tell "wrong name" apart from
 *     "right name, not allowed".
 * wasm32-wasip1.
 */
#include <stdio.h>

extern int pm_metal_shell_exec(const char *name, const char *arg)
	__attribute__((import_module("env"), import_name("pm_metal_shell_exec")));

int main(void)
{
	int allowed = pm_metal_shell_exec("pwd", "");
	int denied = pm_metal_shell_exec("quit", "");
	int not_found = pm_metal_shell_exec("no_such_command", "");

	printf("t3_shell_exec: allowed=%d denied=%d not_found=%d\n", allowed, denied, not_found);
	return 0;
}
