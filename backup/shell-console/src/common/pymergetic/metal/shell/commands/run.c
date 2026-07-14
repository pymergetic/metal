/*
 * Shell builtin — `run`. See run.h.
 */
#include "pymergetic/metal/shell/commands/run.h"

#include <stdlib.h>

#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/shell/handles.h"
#include "pymergetic/metal/util/log.h"

/* Runs on the process's own worker thread (see runtime/process.h's own
 * doc comment on pm_metal_process_exit_cb) — logs the same "exit=%d"
 * message directly onto the handle's own console pane the old
 * run_worker() used to log itself, now decoupled from the process table
 * that tracks it. `cb_ctx` is the pm_metal_shell_handle_t* passed at
 * spawn() time below — still alive here, since handles outlive any
 * single run() of them. */
static void pm_metal_shell_run_on_exit(pm_metal_process_id_t pid, pm_metal_runtime_handle_t handle, int exit_code,
					void *cb_ctx)
{
	pm_metal_shell_handle_t *mh = cb_ctx;

	(void)pid;
	(void)handle;
	pm_metal_util_log_write(mh->sink.out, PM_METAL_LOG_INFO, "exit=%d", exit_code);
}

int pm_metal_shell_cmd_run(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	if (argc < 2) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "usage: run <id> [args...]");
		return -1;
	}

	int id = pm_metal_shell_parse_id(argv[1]);

	if (id < 0 || !g_pm_metal_shell_handles[id - 1].active) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "no such handle: %s", argv[1]);
		return -1;
	}

	pm_metal_shell_handle_t *mh = &g_pm_metal_shell_handles[id - 1];
	int argn = argc - 2;
	char **run_argv = malloc(sizeof(char *) * (size_t)(argn + 1));
	int i;

	/* Not copied — pm_metal_process_spawn() below copies argv itself
	 * before it returns, so pointing straight at mh->sink.label /
	 * argv[]'s own tokens for the duration of that one call is fine. */
	run_argv[0] = mh->sink.label;
	for (i = 0; i < argn; i++) {
		run_argv[i + 1] = argv[i + 2];
	}

	const char *envp[PM_METAL_SHELL_ENV_MAX];
	int envc = pm_metal_shell_env_snapshot(ctx, envp);
	pm_metal_process_id_t pid;
	int rc = pm_metal_process_spawn(mh->handle, argn + 1, run_argv, envc, envp, mh->sink.consumer_in_fd,
					 mh->sink.producer_out_fd, mh->sink.producer_out_fd, mh->sink.out,
					 pm_metal_shell_run_on_exit, mh, &pid);

	free(run_argv);
	if (rc != 0) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "run: failed to start process");
		return -1;
	}
	pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_INFO,
				 "handle=%d pid=%u run started (focus handle to watch live)", id, pid.pid);
	return 0;
}
