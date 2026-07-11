/*
 * Shell — registry, resolver, dispatcher, cwd helper. See shell.h.
 */
#include "pymergetic/metal/shell/shell.h"

#include <stdio.h>
#include <string.h>

#include "pymergetic/metal/port/platform.h"
#include "pymergetic/metal/runtime/process.h"
#include "pymergetic/metal/util/log.h"

#define PM_METAL_SHELL_MAX_COMMANDS 32
#define PM_METAL_SHELL_MAX_TOKENS 16

static pm_metal_shell_command_t g_pm_metal_shell_commands[PM_METAL_SHELL_MAX_COMMANDS];
static int g_pm_metal_shell_command_count;

static int pm_metal_shell_has_suffix(const char *s, const char *suffix)
{
	size_t slen = strlen(s);
	size_t suflen = strlen(suffix);

	return slen >= suflen && strcmp(s + slen - suflen, suffix) == 0;
}

static const pm_metal_shell_command_t *pm_metal_shell_find_native(const char *name)
{
	int i;

	for (i = 0; i < g_pm_metal_shell_command_count; i++) {
		if (strcmp(g_pm_metal_shell_commands[i].name, name) == 0) {
			return &g_pm_metal_shell_commands[i];
		}
	}
	return NULL;
}

static int pm_metal_shell_fill_native(const pm_metal_shell_command_t *cmd, pm_metal_shell_entry_t *out)
{
	if (!cmd) {
		return -1;
	}
	out->kind = PM_METAL_SHELL_ENTRY_NATIVE;
	out->native_fn = cmd->fn;
	out->help = cmd->help;
	return 0;
}

/* `path` is a fully-formed guest-style vfs_root path, already ending in
 * ".wasm". Checks existence quietly first (pm_metal_port_file_exists() —
 * never logs) before actually load_file()ing it: the bare-word case
 * below calls this for *every* command, expecting a miss almost every
 * time (nothing overrides a builtin by default) — going straight to
 * load_file() would print a spurious "read failed" for every single
 * ordinary native command, which is exactly the wrong kind of "clean
 * build" this codebase otherwise insists on. See shell.h's resolve() doc
 * for the exact resolution order this participates in. */
static int pm_metal_shell_try_wasm(const char *path, pm_metal_shell_entry_t *out)
{
	char host_path[PM_METAL_SHELL_PATH_MAX];

	if (pm_metal_runtime_resolve_path(path, host_path, sizeof(host_path)) != 0
	    || !pm_metal_port_file_exists(host_path)) {
		return -1;
	}

	pm_metal_runtime_handle_t h;

	if (pm_metal_runtime_load_file(path, &h) != 0) {
		return -1;
	}
	out->kind = PM_METAL_SHELL_ENTRY_WASM;
	out->wasm_handle = h;
	return 0;
}

int pm_metal_shell_register(const pm_metal_shell_command_t *cmd)
{
	if (!cmd || !cmd->name || !cmd->fn) {
		return -1;
	}
	if (g_pm_metal_shell_command_count >= PM_METAL_SHELL_MAX_COMMANDS) {
		return -1;
	}
	g_pm_metal_shell_commands[g_pm_metal_shell_command_count++] = *cmd;
	return 0;
}

int pm_metal_shell_resolve(const char *name_or_path, pm_metal_shell_entry_t *out)
{
	if (!name_or_path || !out || name_or_path[0] == '\0') {
		return -1;
	}

	if (!strncmp(name_or_path, PM_METAL_SHELL_BIN_PREFIX, strlen(PM_METAL_SHELL_BIN_PREFIX))) {
		return pm_metal_shell_fill_native(
			pm_metal_shell_find_native(name_or_path + strlen(PM_METAL_SHELL_BIN_PREFIX)), out);
	}

	if (!strncmp(name_or_path, PM_METAL_SHELL_BIN_DIR "/", strlen(PM_METAL_SHELL_BIN_DIR) + 1)) {
		char path[PM_METAL_SHELL_PATH_MAX];

		if (pm_metal_shell_has_suffix(name_or_path, ".wasm")) {
			snprintf(path, sizeof(path), "%s", name_or_path);
		} else {
			snprintf(path, sizeof(path), "%s.wasm", name_or_path);
		}
		return pm_metal_shell_try_wasm(path, out);
	}

	if (!strchr(name_or_path, '/')) {
		char path[PM_METAL_SHELL_PATH_MAX];

		snprintf(path, sizeof(path), "%s/%s.wasm", PM_METAL_SHELL_BIN_DIR, name_or_path);
		if (pm_metal_shell_try_wasm(path, out) == 0) {
			return 0;
		}
		return pm_metal_shell_fill_native(pm_metal_shell_find_native(name_or_path), out);
	}

	return -1; /* some other path — not a command, not yet supported */
}

void pm_metal_shell_list_commands(void (*visit)(const pm_metal_shell_command_t *cmd, void *visit_ctx),
				   void *visit_ctx)
{
	int i;

	if (!visit) {
		return;
	}
	for (i = 0; i < g_pm_metal_shell_command_count; i++) {
		visit(&g_pm_metal_shell_commands[i], visit_ctx);
	}
}

static int pm_metal_shell_tokenize(char *line, char **tokens, int max_tokens)
{
	int n = 0;
	char *saveptr;
	char *tok = strtok_r(line, " \t\r\n", &saveptr);

	while (tok && n < max_tokens) {
		tokens[n++] = tok;
		tok = strtok_r(NULL, " \t\r\n", &saveptr);
	}
	return n;
}

int pm_metal_shell_dispatch_line(pm_metal_shell_ctx_t *ctx, char *line)
{
	char *tokens[PM_METAL_SHELL_MAX_TOKENS];
	int ntok = pm_metal_shell_tokenize(line, tokens, PM_METAL_SHELL_MAX_TOKENS);

	if (ntok == 0) {
		return 0;
	}

	pm_metal_shell_entry_t entry;

	if (pm_metal_shell_resolve(tokens[0], &entry) != 0) {
		pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_ERROR, "unknown command: %s (try 'help')",
					 tokens[0]);
		return -1;
	}

	if (entry.kind == PM_METAL_SHELL_ENTRY_NATIVE) {
		return entry.native_fn(ctx, ntok, tokens);
	}

	/* WASM override — synchronous, foreground exec (spawn() then
	 * immediately wait(), so this dispatch call blocks until it
	 * finishes, same as a real shell running a foreground command):
	 * load_file() already happened inside resolve(); run it now with
	 * this dispatch's own argv (argv[0] is exactly as typed, e.g.
	 * "ls", not the resolved "/bin/ls.wasm" path — matches a real
	 * program's own argv[0] convention) and ctx's own exported env
	 * (see pm_metal_shell_env_snapshot()), and always unload() it
	 * afterward, win or lose. Routed through runtime/process.h (rather
	 * than calling run_ex() directly) purely for uniformity — every
	 * guest execution this codebase starts, foreground or
	 * backgrounded via `run`, is a tracked process the same way. */
	const char *envp[PM_METAL_SHELL_ENV_MAX];
	int envc = pm_metal_shell_env_snapshot(ctx, envp);
	pm_metal_process_id_t pid;
	int exit_code = -1;

	if (pm_metal_process_spawn(entry.wasm_handle, ntok, tokens, envc, envp, ctx->sink->consumer_in_fd,
				    ctx->sink->producer_out_fd, ctx->sink->producer_out_fd, NULL, NULL,
				    &pid) != 0
	    || pm_metal_process_wait(pid, &exit_code) != 0) {
		exit_code = -1;
	}

	pm_metal_util_log_write(ctx->sink->out, PM_METAL_LOG_INFO, "%s: exit=%d", tokens[0], exit_code);
	pm_metal_runtime_unload(entry.wasm_handle);
	return exit_code;
}

void pm_metal_shell_resolve_path(const char *cwd, const char *arg, char *out, size_t out_sz)
{
	char joined[PM_METAL_SHELL_PATH_MAX * 2];

	if (arg[0] == '/') {
		snprintf(joined, sizeof(joined), "%s", arg);
	} else if (!strcmp(cwd, "/")) {
		snprintf(joined, sizeof(joined), "/%s", arg);
	} else {
		snprintf(joined, sizeof(joined), "%s/%s", cwd, arg);
	}

	/* Lexical normalize: split on '/', drop "" and ".", pop one level on
	 * ".." (popping past the root is a no-op, not an error — same as
	 * `cd /../..` in a real shell just landing back at `/`). */
	char work[sizeof(joined)];
	char *segments[64];
	int nseg = 0;
	char *saveptr;
	char *tok;

	snprintf(work, sizeof(work), "%s", joined);
	tok = strtok_r(work, "/", &saveptr);
	while (tok) {
		if (!strcmp(tok, ".")) {
			/* skip */
		} else if (!strcmp(tok, "..")) {
			if (nseg > 0) {
				nseg--;
			}
		} else if (nseg < (int)(sizeof(segments) / sizeof(segments[0]))) {
			segments[nseg++] = tok;
		}
		tok = strtok_r(NULL, "/", &saveptr);
	}

	char rebuilt[sizeof(joined)];
	size_t pos = 0;
	int i;

	for (i = 0; i < nseg; i++) {
		size_t seg_len = strlen(segments[i]);

		if (pos + 1 + seg_len >= sizeof(rebuilt)) {
			break;
		}
		rebuilt[pos++] = '/';
		memcpy(rebuilt + pos, segments[i], seg_len);
		pos += seg_len;
	}
	if (pos == 0) {
		rebuilt[pos++] = '/';
	}
	rebuilt[pos] = '\0';

	snprintf(out, out_sz, "%s", rebuilt);
}

int pm_metal_shell_env_set(pm_metal_shell_ctx_t *ctx, const char *entry)
{
	const char *eq = strchr(entry, '=');

	if (!eq) {
		return -1;
	}

	size_t key_len = (size_t)(eq - entry);
	int i;

	for (i = 0; i < ctx->env_count; i++) {
		if (!strncmp(ctx->env[i], entry, key_len) && ctx->env[i][key_len] == '=') {
			snprintf(ctx->env[i], sizeof(ctx->env[i]), "%s", entry);
			return 0;
		}
	}
	if (ctx->env_count >= PM_METAL_SHELL_ENV_MAX) {
		return -1;
	}
	snprintf(ctx->env[ctx->env_count], sizeof(ctx->env[ctx->env_count]), "%s", entry);
	ctx->env_count++;
	return 0;
}

int pm_metal_shell_env_snapshot(const pm_metal_shell_ctx_t *ctx, const char *out_envp[PM_METAL_SHELL_ENV_MAX])
{
	int i;

	for (i = 0; i < ctx->env_count; i++) {
		out_envp[i] = ctx->env[i];
	}
	return ctx->env_count;
}
