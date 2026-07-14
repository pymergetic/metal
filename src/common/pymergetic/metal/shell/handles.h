/*
 * Shell — the loaded-handle table + kernel-sink/quit-cb globals every
 * per-command file under shell/commands/ that touches a handle (load, run,
 * unload, focus, ps, quit) needs. Private — NOT part of shell/commands.h's
 * public contract, shared only between shell/commands.c and its split
 * files under shell/commands/ (same "private header between one module's
 * own split parts" pattern as console/viewport_local.h). One directory up
 * from shell/commands/ on purpose: this is shared infrastructure, not a
 * command itself, so it does not get a commands/<name>.{h,c} pair of its
 * own. Nothing outside those should include this.
 */
#ifndef PYMERGETIC_METAL_SHELL_HANDLES_H_
#define PYMERGETIC_METAL_SHELL_HANDLES_H_

#include "pymergetic/metal/console/console.h"
#include "pymergetic/metal/runtime/runtime.h"

/* Sized against PM_METAL_RUNTIME_MAX_HANDLES — runtime.h's own public
 * constant (see there) for exactly this: every id runtime.c can hand out
 * maps to a valid slot here, with nothing to keep in sync by hand.
 *
 * No worker/run-tracking fields here — a handle may have any number of
 * concurrent `run`s against it (see runtime/process.h), so that
 * bookkeeping lives in the process table, keyed by pid rather than handle
 * id. This struct is purely "is this handle loaded, and which console is
 * it wired to". */
typedef struct pm_metal_shell_handle {
	int active;
	pm_metal_runtime_handle_t handle;
	pm_metal_console_sink_t sink;
} pm_metal_shell_handle_t;

/* impl: common — src/common/pymergetic/metal/shell/handles.c
 *
 * Defined there, written only by pm_metal_shell_handles_init() (zeroing)
 * and each command that loads/unloads a handle; read by every other
 * command that takes an <id>. `g_pm_metal_shell_kernel_sink`/
 * `g_pm_metal_shell_quit_cb` are set once by handles_init() and read-only
 * after (see commands.h's own doc comment on handles_init()). */
extern pm_metal_shell_handle_t g_pm_metal_shell_handles[PM_METAL_RUNTIME_MAX_HANDLES];
extern pm_metal_console_sink_t *g_pm_metal_shell_kernel_sink;
extern void (*g_pm_metal_shell_quit_cb)(void);

/* impl: common — src/common/pymergetic/metal/shell/handles.c
 *
 * Parses `tok` as a 1-based handle id in range. Returns the id, or -1
 * (not a plain positive integer, or outside "1..PM_METAL_RUNTIME_MAX_HANDLES")
 * — the caller still has to separately check g_pm_metal_shell_handles[id
 * - 1].active, a syntactically valid id is not necessarily a loaded one. */
int pm_metal_shell_parse_id(const char *tok);

#endif /* PYMERGETIC_METAL_SHELL_HANDLES_H_ */
