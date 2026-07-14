/*
 * Shell builtin — `ps`. See ps.h.
 */
#include "pymergetic/metal/shell/commands/ps.h"

#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/shell/handles.h"
#include "pymergetic/metal/util/log.h"

static void pm_metal_shell_cmd_ps_visit_process(const pm_metal_process_info_t *info, void *raw)
{
	pm_metal_shell_ctx_t *ctx = raw;

	if (info->running) {
		pm_metal_util_log_write_raw(ctx->sink->out, "  pid=%u handle=%u cmd=%s (running)", info->pid.pid,
					     info->handle.id, info->cmd);
	} else {
		pm_metal_util_log_write_raw(ctx->sink->out, "  pid=%u handle=%u cmd=%s exit=%d", info->pid.pid,
					     info->handle.id, info->cmd, info->exit_code);
	}
}

/* Two sections, one command (see the earlier `list` vs `ps` split this
 * replaced): loaded handles first (what `run <id>`/`unload <id>` target,
 * whether or not anything is currently running against them), then every
 * tracked process (see runtime/process.h) — 0, 1, or several per handle,
 * decoupled from it. pm_metal_process_list() is read-only, so calling
 * `ps` never itself reaps a finished process (see there). */
int pm_metal_shell_cmd_ps(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	(void)argc;
	(void)argv;

	pm_metal_util_log_write_raw(ctx->sink->out, "kernel (sink 0)");

	int i;

	for (i = 0; i < PM_METAL_RUNTIME_MAX_HANDLES; i++) {
		if (!g_pm_metal_shell_handles[i].active) {
			continue;
		}
		pm_metal_util_log_write_raw(ctx->sink->out, "handle=%d label=%s", i + 1,
					     g_pm_metal_shell_handles[i].sink.label);
	}

	pm_metal_util_log_write_raw(ctx->sink->out, "processes:");
	pm_metal_process_list(pm_metal_shell_cmd_ps_visit_process, ctx);
	return 0;
}
