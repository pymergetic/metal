/*
 * Shell — command registry, virtual `/bin/pm` namespace, and cwd. Host-only
 * (never built into a mod), impl: common (zero OS dependency — everything
 * here is either pure string/array logic or a call into the already-common
 * runtime.h contract). See docs/CONSOLE.md "Shell" for the full picture.
 *
 * Two kinds of resolvable "command", one dispatch path:
 *
 *   native  — a C function registered via pm_metal_shell_register(),
 *     reachable as a bare word ("load") or the explicit virtual path
 *     PM_METAL_SHELL_BIN_PREFIX + name ("/bin/pm/load"). Always present —
 *     this is the fallback layer, and the explicit /bin/pm/<name> form is
 *     an escape hatch that always reaches it even if shadowed (below).
 *
 *   wasm override — a real ".wasm" file under PM_METAL_SHELL_BIN_DIR in
 *     the guest-facing vfs_root (e.g. "/bin/ls.wasm"). Checked *before*
 *     the native registry for a bare word: if pm_metal_runtime_load_file()
 *     on "/bin/<name>.wasm" succeeds, it wins, even over a same-named
 *     native command — dropping a real busybox-style ls.wasm into vfs_root
 *     immediately starts shadowing the native `ls`, no restart needed
 *     (resolve() never caches — it re-checks the real file on every
 *     dispatch). See pm_metal_shell_resolve()'s own doc comment below for
 *     the exact resolution order.
 *
 * cwd lives on pm_metal_shell_ctx_t, one per console (kernel's own, and —
 * if a handle ever gets its own interactive shell later — one per handle);
 * it navigates the same real vfs_root tree modules/data already live in,
 * entirely independent of command resolution (typing a bare command name
 * is found the same way regardless of cwd, exactly like a real shell's
 * $PATH search does not care where you currently are).
 */
#ifndef PYMERGETIC_METAL_SHELL_SHELL_H_
#define PYMERGETIC_METAL_SHELL_SHELL_H_

#include <stddef.h>

#include "pymergetic/metal/console/console.h"
#include "pymergetic/metal/runtime/runtime.h"

#define PM_METAL_SHELL_BIN_PREFIX "/bin/pm/" /* virtual — native registry, never touches disk */
#define PM_METAL_SHELL_BIN_DIR    "/bin"     /* real — vfs_root dir for override .wasm files */
#define PM_METAL_SHELL_PATH_MAX   256
#define PM_METAL_SHELL_ENV_MAX       16  /* max exported "KEY=VALUE" entries, per console */
#define PM_METAL_SHELL_ENV_ENTRY_MAX 128 /* max length of one "KEY=VALUE" entry, incl. '\0' */

typedef struct pm_metal_shell_ctx {
	pm_metal_console_sink_t *sink; /* where to write output / read input for this console */
	char cwd[PM_METAL_SHELL_PATH_MAX]; /* always absolute, vfs_root-relative; start at "/" */

	/* `export`ed "KEY=VALUE" entries, one per console (like cwd — not
	 * shared/inherited across consoles, and not the host's own libc
	 * environ, which this codebase never touches). Handed to every
	 * `run`/wasm-override guest this console starts as its WASI env
	 * (see runtime.h's run_ex() envc/envp) — see
	 * pm_metal_shell_env_snapshot() below for the exact hand-off. */
	char env[PM_METAL_SHELL_ENV_MAX][PM_METAL_SHELL_ENV_ENTRY_MAX];
	int env_count;
} pm_metal_shell_ctx_t;

/* Same shape as a wasm mod's own main(argc, argv) + exit code — see
 * shell.h's own header comment on why: it is what lets dispatch_line()
 * treat a native command and a wasm-override command almost identically
 * from the caller's side. */
typedef int (*pm_metal_shell_cmd_fn)(pm_metal_shell_ctx_t *ctx, int argc, char **argv);

typedef struct pm_metal_shell_command {
	const char *name;
	pm_metal_shell_cmd_fn fn;
	const char *help;

	/* Whether shell/guest_exec.c's native-import bridge (a running WASM
	 * guest calling pm_metal_shell_guest_exec() below directly, not a
	 * human at a console) is allowed to invoke this one. Irrelevant to
	 * every other caller here — dispatch_line() (a human, or a .wasm
	 * command's own foreground exec) never consults this field, and
	 * ls/help's registry listing still shows every command regardless
	 * (see pm_metal_shell_list_commands()) — a denied command stays
	 * *visible*, only guest *invocation* of it is refused. Defaults to
	 * 0 (denied) for anything that doesn't explicitly opt in — see
	 * shell/commands.c's registration table for which ones do and why. */
	int guest_callable;
} pm_metal_shell_command_t;

/* pm_metal_shell_guest_exec()'s two failure returns — chosen well outside
 * the range any real command's own pm_metal_shell_cmd_fn return value
 * could plausibly land in (today: 0 on success, small negative like -1
 * on a handled error) so a guest can tell "the command itself failed"
 * apart from "that wasn't even attempted". */
#define PM_METAL_SHELL_EXEC_NOT_FOUND (-1000) /* no such registered native command */
#define PM_METAL_SHELL_EXEC_DENIED    (-1001) /* registered, but guest_callable == 0 */

typedef enum pm_metal_shell_entry_kind {
	PM_METAL_SHELL_ENTRY_NATIVE = 0,
	PM_METAL_SHELL_ENTRY_WASM,
} pm_metal_shell_entry_kind_t;

/* Resolved once per dispatch — never cached, so a .wasm dropped into
 * PM_METAL_SHELL_BIN_DIR takes effect on the very next call, no restart.
 * kind == WASM means resolve() already called pm_metal_runtime_load_file()
 * successfully; the caller (pm_metal_shell_dispatch_line()) owns running
 * it exactly once and then always unload()ing wasm_handle, win or lose. */
typedef struct pm_metal_shell_entry {
	pm_metal_shell_entry_kind_t kind;
	pm_metal_shell_cmd_fn native_fn; /* kind == NATIVE */
	const char *help;                /* kind == NATIVE */
	pm_metal_runtime_handle_t wasm_handle; /* kind == WASM — already load()ed */
} pm_metal_shell_entry_t;

/* impl: common — src/common/pymergetic/metal/shell/shell.c
 *
 * Adds one entry to the native registry. `cmd` is copied (name/help
 * pointers are kept as-is — pass string literals or otherwise
 * process-lifetime-stable strings, same convention as console_sink_t's
 * label). Returns 0/-1 (registry full, or cmd/name/fn missing). */
int pm_metal_shell_register(const pm_metal_shell_command_t *cmd);

/* impl: common — src/common/pymergetic/metal/shell/shell.c
 *
 * Resolution order for `name_or_path`:
 *   - starts with PM_METAL_SHELL_BIN_PREFIX ("/bin/pm/") -> strip prefix,
 *     native registry ONLY — the explicit escape hatch, never shadowed by
 *     a same-named .wasm override.
 *   - starts with PM_METAL_SHELL_BIN_DIR "/" ("/bin/") -> literal file
 *     reference; appends ".wasm" if not already present, then tries
 *     pm_metal_runtime_load_file() on exactly that path — found -> WASM;
 *     not found -> error, no fallback (an explicit path that doesn't
 *     exist is just wrong, same as a real shell).
 *   - bare name (no '/' at all) -> first tries
 *     pm_metal_runtime_load_file(PM_METAL_SHELL_BIN_DIR "/<name>.wasm")
 *     (the override check — expected to usually fail, since nothing's
 *     there by default); on success, WASM wins even over a same-named
 *     native command. On failure, falls back to the native registry by
 *     exact name.
 *   - anything else (contains '/' but isn't under /bin) -> not a command,
 *     not yet supported; returns -1.
 * Returns 0 (found, *out filled) or -1 (not found, *out untouched).
 *
 * Caveat: the override check's "does it exist" and "is it valid wasm" are
 * the same single load_file() call — a same-named .wasm that exists but
 * fails to parse falls back to the native command (or "not found") rather
 * than surfacing a distinct parse error. Acceptable for a first pass. */
int pm_metal_shell_resolve(const char *name_or_path, pm_metal_shell_entry_t *out);

/* impl: common — src/common/pymergetic/metal/shell/shell.c
 *
 * Visits every *native* registry entry (in registration order) — used by
 * the `help` and `ls /bin/pm` builtins (see shell/commands/help.c, ls.c).
 * Wasm overrides are not enumerable this way — listing real ".wasm" files
 * under /bin needs a real host readdir, which shell/commands/ls.c's `ls`
 * now has (see port/dir.h) but this registry-only helper does not. */
void pm_metal_shell_list_commands(void (*visit)(const pm_metal_shell_command_t *cmd, void *visit_ctx),
				   void *visit_ctx);

/* impl: common — src/common/pymergetic/metal/shell/shell.c
 *
 * Tokenizes `line` (mutated in place, like strtok — same convention the
 * old src/linux/main.c tokenizer used), resolves argv[0], and either
 * calls the native handler directly or (WASM) run_ex()s + unload()s the
 * resolved handle, logging "argv[0]: exit=%d" onto ctx->sink the same way
 * scripted mode's own output looks. An empty/whitespace-only line is a
 * silent no-op (returns 0). An unresolvable argv[0] logs an error onto
 * ctx->sink and returns -1. Otherwise returns the command's own exit
 * code/return value. */
int pm_metal_shell_dispatch_line(pm_metal_shell_ctx_t *ctx, char *line);

/* impl: common — src/common/pymergetic/metal/shell/shell.c
 *
 * The guest-invocation counterpart to dispatch_line() above — called
 * from shell/guest_exec.c's native-import bridge, never from a human
 * console. Looks `name` up in the *native registry only* (no
 * PM_METAL_SHELL_BIN_DIR wasm-override check — a guest invoking another
 * guest's .wasm override is out of scope for this call) and:
 *   - PM_METAL_SHELL_EXEC_NOT_FOUND if no such command is registered;
 *   - PM_METAL_SHELL_EXEC_DENIED if it is, but guest_callable == 0;
 *   - otherwise calls it — cmd->fn(ctx, argc, argv) with argv = {name}
 *     (argc 1) or {name, arg} (argc 2, iff arg[0] != '\0') — and
 *     returns its own exit code.
 * `arg` must be non-NULL (pass "" for "no argument", never NULL) — see
 * shell/guest_exec.c, the one caller, on why the wasm-side signature
 * makes that guarantee rather than this function defending against
 * NULL itself. `ctx` is the caller's own throwaway context (see
 * guest_exec.c on why it's not the same persistent per-console
 * cwd/env this function's dispatch_line() sibling gets). */
int pm_metal_shell_guest_exec(pm_metal_shell_ctx_t *ctx, const char *name, const char *arg);

/* impl: common — src/common/pymergetic/metal/shell/shell.c
 *
 * cwd-join + lexical "."/".." normalize (never touches the real
 * filesystem — see shell/commands/cd.c's `cd` for why). `arg` starting with '/'
 * passes through unchanged (still normalized) — same "absolute overrides
 * cwd" rule every command's own path argument follows. `out`/`out_sz`
 * follow snprintf() truncation semantics. */
void pm_metal_shell_resolve_path(const char *cwd, const char *arg, char *out, size_t out_sz);

/* impl: common — src/common/pymergetic/metal/shell/shell.c
 *
 * Sets (adds, or replaces by exact "KEY=" match) one "KEY=VALUE" entry on
 * ctx->env — the `export` builtin's entire implementation (see
 * shell/commands/export.c); not itself a builtin. Returns 0/-1 (`entry` has no
 * '=', or ctx->env is already at PM_METAL_SHELL_ENV_MAX and this is a new
 * key, not a replacement). */
int pm_metal_shell_env_set(pm_metal_shell_ctx_t *ctx, const char *entry);

/* impl: common — src/common/pymergetic/metal/shell/shell.c
 *
 * Fills out_envp[0..count) with pointers straight into ctx->env's own
 * storage — no copying (runtime/process.h's spawn() copies whatever it's
 * handed itself, so pointing at ctx->env for the brief duration of one
 * spawn() call is safe; just don't export() concurrently with a spawn()
 * using this same snapshot on the same ctx). Returns ctx->env_count. */
int pm_metal_shell_env_snapshot(const pm_metal_shell_ctx_t *ctx, const char *out_envp[PM_METAL_SHELL_ENV_MAX]);

#endif /* PYMERGETIC_METAL_SHELL_SHELL_H_ */
