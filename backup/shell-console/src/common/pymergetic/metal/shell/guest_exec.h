/*
 * Shell — guest-exec bridge. Host-only (never built into a mod), but the
 * one file under shell/ that exists purely to let a WASM guest reach a
 * subset of the native command registry directly — see docs/CONSOLE.md
 * "Guest-callable commands", shell.h's own doc comment on the
 * `guest_callable` field, and shell.c's pm_metal_shell_guest_exec().
 *
 * WASI's own path_open()/fd_read() has no "exec another program"
 * concept — a native command is a host C function call, not a byte
 * stream a guest's file I/O could ever reach. So this is a genuine new
 * capability, not a VFS trick: a WAMR native-import function
 * ("pm_metal_shell_exec(name, arg) -> i32", registered under the "env"
 * module) a guest calls directly, like a syscall. `arg` is always a
 * valid string on the wasm side, `""` meaning "no argument" — never a
 * null pointer, so the native side never has to special-case one (see
 * guest_exec.c).
 *
 * Scope: works from *any* execution runtime/process.h's spawn() started
 * — console `run`, a .wasm PATH override's foreground dispatch, and
 * scripted mode alike, not just a module `load`ed through the
 * interactive console. Resolution is per-*process* (pid), not per-
 * handle: runtime.h's run_ex() tags each instance with the pid
 * process.c's spawn() gave it (its `custom_tag` param, see there), and
 * this file resolves that pid back to wherever spawn()'s own
 * `guest_out` FILE* pointed it at (a real console sink's ->out for
 * console `run`/wasm-override dispatch, plain host stdout for scripted
 * mode — see process.h's spawn() doc comment and each of its three call
 * sites). Pid-grained rather than handle-grained on purpose: the same
 * handle can have several processes in flight at once (process.h), and
 * it is specifically *this* execution's own output that must be used,
 * not some other concurrent one against the same handle. The only way
 * to get NOT_FOUND for a reason other than "no such command name" is an
 * instance run directly via runtime.h's run()/run_ex() bypassing
 * process.h entirely (custom_tag defaults to 0, never a real pid) — not
 * a path anything under shell/ or app/ actually takes today. */
#ifndef PYMERGETIC_METAL_SHELL_GUEST_EXEC_H_
#define PYMERGETIC_METAL_SHELL_GUEST_EXEC_H_

/* impl: common — src/common/pymergetic/metal/shell/guest_exec.c
 *
 * Registers the native import above with WAMR (wasm_runtime_register_
 * natives() — a wasm_export.h API, so WAMR itself must already be
 * initialized, i.e. call this after pm_metal_runtime_init() returns) and
 * before any pm_metal_runtime_load_file()/load_bytes() — natives must be
 * registered before a module importing them gets instantiated. Call
 * exactly once per process (see app/app.c's two entry points, the only
 * callers). Idempotent-vs.-crash is not this function's job to guarantee
 * beyond that: call it once. */
void pm_metal_shell_guest_exec_register(void);

#endif /* PYMERGETIC_METAL_SHELL_GUEST_EXEC_H_ */
