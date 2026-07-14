/*
 * App — see app.h. Ported straight from src/linux/main.c's own
 * run_console_mode()/shutdown_console_mode()/kernel_dispatch_thread()/
 * run_scripted_mode(): raw pthread_create()/pthread_join() -> port/worker.h,
 * and the raw viewport_feed_fd close() -> console.h's own
 * pm_metal_console_stop_feed() — the two things that used to make this
 * impl: bind by necessity. One behavior change since that port: Ctrl+C/
 * SIGINT now runs the same full teardown as `quit`/EOF instead of the
 * OS's own default "just die" action — see port/intr.h. Both entry
 * points also register shell/guest_exec.h's native import before their
 * first load_file() — see there.
 */
#include "pymergetic/metal/app/app.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/console/console.h"
#include "pymergetic/metal/console/viewport.h"
#include "pymergetic/metal/port/intr.h"
#include "pymergetic/metal/port/worker.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/runtime/runtime.h"
#include "pymergetic/metal/shell/commands.h"
#include "pymergetic/metal/shell/guest_exec.h"
#include "pymergetic/metal/shell/shell.h"
#include "pymergetic/metal/util/log.h"

#define PM_METAL_APP_LINE_MAX 512

static const char *pm_metal_app_basename_of(const char *path)
{
	const char *slash = strrchr(path, '/');

	return slash ? slash + 1 : path;
}

int pm_metal_app_run_scripted(const char *argv0, int wasm_argc, char **wasm_argv)
{
	/* register_builtins(): populates shell.c's native registry (pure
	 * static-table bookkeeping — no dependency on shell/handles.c's
	 * console-mode-only table) — needed here too, not just console
	 * mode's own call further below, so that a guest calling
	 * shell/guest_exec.h's native import from a scripted-mode module
	 * (see mods/t3_shell_exec/main.c, scripts/verify-linux.sh) finds
	 * anything registered to resolve "pwd" et al. against; harmless
	 * to call from both run modes since a process only ever runs one
	 * of them. guest_exec_register(): must precede every load_file()
	 * below — natives have to be registered before a module importing
	 * them is instantiated (see guest_exec.h). pm_metal_runtime_init()
	 * (called by main.c before this) has already brought WAMR itself
	 * up, so both are safe here. */
	pm_metal_shell_register_builtins();
	pm_metal_shell_guest_exec_register();

	int rc = 0;
	int i;

	for (i = 0; i < wasm_argc; i++) {
		const char *path = wasm_argv[i];
		pm_metal_runtime_handle_t h;

		if (pm_metal_runtime_load_file(path, &h) != 0) {
			fprintf(stderr, "%s: load failed: %s\n", argv0, path);
			rc = 1;
			continue;
		}

		char *mod_argv[1];
		mod_argv[0] = (char *)pm_metal_app_basename_of(path);

		/* spawn()+wait() rather than a direct run(): one consistent
		 * model everywhere a guest executes, console and scripted
		 * mode alike (see runtime/process.h) — this stays exactly
		 * as sequential/blocking as a direct run() itself was, just
		 * now also visible to anything else that might list the
		 * process table while it's in flight (nothing does, today —
		 * scripted mode has no console/shell). guest_out=stdout: no
		 * console sink exists here (see above), but a guest calling
		 * shell/guest_exec.c's bridge should still land somewhere
		 * visible rather than nowhere — real stdout, interleaved
		 * with this same module's own prints just above/below,
		 * mirrors -1's own "inherit real stdio" meaning for
		 * stdin_fd/stdout_fd/stderr_fd right next to it. */
		pm_metal_process_id_t pid;
		int exit_code;

		if (pm_metal_process_spawn(h, 1, mod_argv, 0, NULL, -1, -1, -1, stdout, NULL, NULL, &pid) != 0
		    || pm_metal_process_wait(pid, &exit_code) != 0) {
			fprintf(stderr, "%s: run failed: %s\n", argv0, path);
			exit_code = -1;
		}

		printf("%s: exit=%d\n", path, exit_code);
		fflush(stdout);
		if (exit_code != 0) {
			rc = 1;
		}

		pm_metal_runtime_unload(h);
	}

	pm_metal_process_shutdown();
	pm_metal_runtime_shutdown();
	return rc;
}

/* ---- console mode ---- */

static pm_metal_console_sink_t g_pm_metal_app_kernel_sink;
static pm_metal_shell_ctx_t g_pm_metal_app_kernel_shell_ctx;

/* Set by the `quit`/`exit` builtin's callback (see
 * pm_metal_shell_handles_init() below), polled by the calling thread's
 * pump loop — see pm_metal_app_run_console(). All the actual teardown
 * runs on the calling thread only, *after* the pump loop has
 * confirmed-stopped and the dispatcher thread has been joined — never
 * concurrently with pump()/register()/etc, which is what calling
 * console_close()/viewport_shutdown() straight from the dispatcher
 * thread used to do (a real bug: it raced viewport.c's shared state
 * against the still-running pump loop). volatile, not atomic: a single
 * writer (the dispatcher thread, via the callback below), read-only
 * elsewhere, coarse polling — same class of knob as util/log.h's level,
 * see its docs. */
static volatile int g_pm_metal_app_quit_requested;

/* The `quit`/`exit` builtin's callback (see shell/commands.h) — runs on
 * the kernel dispatcher thread. Dispatcher-thread-safe: only sets the
 * flag the pump loop polls — no shared console/viewport state touched
 * here, that all happens after the pump loop has actually stopped,
 * below. */
static void pm_metal_app_request_quit(void)
{
	g_pm_metal_app_quit_requested = 1;
}

static int pm_metal_app_kernel_dispatch_thread(void *arg)
{
	(void)arg;

	char line[PM_METAL_APP_LINE_MAX];

	/* No g_pm_metal_app_quit_requested check in the loop condition on
	 * purpose: this thread's only job is to keep draining/processing
	 * whatever is *already* buffered in the kernel sink's own pipe, in
	 * order, until either `quit`/`exit` sets the flag itself (see
	 * pm_metal_app_request_quit()) or that pipe hits real EOF (the
	 * pump loop below stops feeding it via
	 * pm_metal_console_stop_feed() once it itself stops). A quit
	 * command does not need to break out of this loop early: it just
	 * falls through to the next (blocking) fgets() call, which the
	 * stop_feed() below unblocks exactly like any other shutdown path
	 * — checking the flag here too would let the pump loop's *own*
	 * stop reason (e.g. real stdin EOF, racing independently) cut this
	 * thread off mid-drain and silently skip a command that was
	 * already delivered to it. */
	while (fgets(line, sizeof(line), g_pm_metal_app_kernel_sink.in)) {
		pm_metal_shell_dispatch_line(&g_pm_metal_app_kernel_shell_ctx, line);
	}
	return 0;
}

/* Calling-thread-only, called from pm_metal_app_run_console() after its
 * pump loop has broken out (never while pump() might still be running —
 * see g_pm_metal_app_quit_requested's doc comment) — safe to touch every
 * sink/viewport/runtime structure directly and unlocked. */
static void pm_metal_app_shutdown_console(void)
{
	/* A command right before `quit` (e.g. `ps`) may still have output
	 * sitting unread in its sink's pipe — the pump loop that used to
	 * drain it just stopped. We are the only thread touching viewport
	 * state now (dispatcher already joined), so a couple more pump()
	 * calls here are safe and flush anything left before sinks close. */
	int flush;

	for (flush = 0; flush < 2; flush++) {
		pm_metal_viewport_pump(PM_METAL_VIEWPORT_LOCAL);
	}

	/* Must run before pm_metal_shell_handles_shutdown() — see
	 * shell/commands.h: joins out every in-flight `run`/wasm-override
	 * process against every handle first, so the unload() loop inside
	 * handles_shutdown() below is never refused as "busy". */
	pm_metal_process_shutdown();
	pm_metal_shell_handles_shutdown();
	pm_metal_console_close(&g_pm_metal_app_kernel_sink);
	pm_metal_runtime_shutdown();
}

int pm_metal_app_run_console(const char *argv0, const char *vfs_root_abs, int wasm_argc, char **wasm_argv)
{
	pm_metal_runtime_handle_t no_handle = {0};

	if (pm_metal_console_open(PM_METAL_CONSOLE_KERNEL, no_handle, "kernel", &g_pm_metal_app_kernel_sink) != 0) {
		fprintf(stderr, "%s: console open failed\n", argv0);
		pm_metal_runtime_shutdown();
		return 1;
	}
	if (pm_metal_viewport_init(PM_METAL_VIEWPORT_LOCAL) != 0
	    || pm_metal_viewport_register(PM_METAL_VIEWPORT_LOCAL, &g_pm_metal_app_kernel_sink) != 0
	    || pm_metal_viewport_focus(PM_METAL_VIEWPORT_LOCAL, &g_pm_metal_app_kernel_sink) != 0) {
		fprintf(stderr, "%s: viewport init failed\n", argv0);
		pm_metal_console_close(&g_pm_metal_app_kernel_sink);
		pm_metal_runtime_shutdown();
		return 1;
	}

	g_pm_metal_app_kernel_shell_ctx.sink = &g_pm_metal_app_kernel_sink;
	snprintf(g_pm_metal_app_kernel_shell_ctx.cwd, sizeof(g_pm_metal_app_kernel_shell_ctx.cwd), "/");
	pm_metal_shell_handles_init(&g_pm_metal_app_kernel_sink, pm_metal_app_request_quit);
	pm_metal_shell_register_builtins();
	/* Must precede every load_file() below (the preload loop right
	 * after this, and every `load` typed at the prompt later) — see
	 * guest_exec.h on why natives have to be registered before any
	 * module importing them is instantiated. */
	pm_metal_shell_guest_exec_register();

	pm_metal_util_log_write(g_pm_metal_app_kernel_sink.out, PM_METAL_LOG_INFO,
				 "%s console — type 'help'. vfs_root=%s", pm_metal_app_basename_of(argv0),
				 vfs_root_abs);

	/* Positional .wasm paths are pre-loaded (not run) before the
	 * dispatcher starts — same load() semantics as typing `load <path>`
	 * for each, just a shortcut for "have these ready". */
	int i;

	for (i = 0; i < wasm_argc; i++) {
		char line[PM_METAL_APP_LINE_MAX];

		snprintf(line, sizeof(line), "load %s", wasm_argv[i]);
		pm_metal_shell_dispatch_line(&g_pm_metal_app_kernel_shell_ctx, line);
	}

	pm_metal_port_worker_t dispatch_worker;

	if (pm_metal_port_worker_spawn(&dispatch_worker, pm_metal_app_kernel_dispatch_thread, NULL) != 0) {
		fprintf(stderr, "%s: failed to start kernel dispatcher\n", argv0);
		pm_metal_viewport_shutdown(PM_METAL_VIEWPORT_LOCAL);
		pm_metal_console_close(&g_pm_metal_app_kernel_sink);
		pm_metal_runtime_shutdown();
		return 1;
	}

	/* Installed right before the loop that polls it — see port/intr.h.
	 * Ctrl+C/SIGINT must run the exact same teardown below as `quit`/
	 * EOF, never the OS's own default "just die" action, which would
	 * skip unload()ing every handle. */
	pm_metal_port_intr_install();

	for (;;) {
		if (pm_metal_viewport_pump(PM_METAL_VIEWPORT_LOCAL) != 0) {
			g_pm_metal_app_quit_requested = 1; /* real stdin EOF (piped input exhausted, or Ctrl-D) also means "stop" */
			break;
		}
		if (pm_metal_port_intr_requested()) {
			pm_metal_util_log_write(g_pm_metal_app_kernel_sink.out, PM_METAL_LOG_INFO,
						 "interrupted — shutting down...");
			break;
		}
		if (g_pm_metal_app_quit_requested) {
			break;
		}
	}

	/* The dispatcher only ever receives new bytes via the pump loop just
	 * above (real stdin routed to whichever sink is focused) — now that
	 * it has stopped, a dispatcher blocked in fgets() on the kernel
	 * sink's `in` would never wake on its own; stop_feed() makes that
	 * blocking read return EOF instead, so the join below cannot hang. */
	pm_metal_console_stop_feed(&g_pm_metal_app_kernel_sink);
	pm_metal_port_worker_join(&dispatch_worker);

	pm_metal_app_shutdown_console();
	printf("%s: shutdown complete\n", pm_metal_app_basename_of(argv0)); /* real stdout, unconditionally — panes are gone by now */
	return 0;
}
