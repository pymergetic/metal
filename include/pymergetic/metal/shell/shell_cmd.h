/*
 * Metal shell — registered host commands.
 *
 * Topic modules define commands beside their domain code with
 * PM_METAL_SHELL_CMD / PM_METAL_SHELL_CMDS (linker section
 * `.pm_metal_shell_cmds.*`). Each module contributes one
 * pm_metal_shell_cmd_table_t (16-byte aligned); install walks the section.
 *
 * Handlers receive Unix-style argc/argv (argv[0] == command name).
 *
 * impl: common — src/pymergetic/metal/shell/shell/shell_cmd.c
 */
#ifndef PYMERGETIC_METAL_SHELL_SHELL_CMD_H_
#define PYMERGETIC_METAL_SHELL_SHELL_CMD_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/** Max tokens per line (including argv[0]). */
#define PM_METAL_SHELL_ARGV_MAX  16u

typedef void (*pm_metal_shell_cmd_fn)(int argc, char **argv);

typedef struct pm_metal_shell_cmd {
	const char *name;
	const char *help;
	pm_metal_shell_cmd_fn fn;
} pm_metal_shell_cmd_t;

/** One module's command list — always 16 bytes so section walk is LTO-safe. */
typedef struct pm_metal_shell_cmd_table {
	const pm_metal_shell_cmd_t *cmds;
	uint32_t count;
	uint8_t _pad[16u - sizeof(void *) - sizeof(uint32_t)];
} pm_metal_shell_cmd_table_t;


/** Max commands gathered from all tables (raise if install truncates). */
#define PM_METAL_SHELL_CMD_MAX  128u

/**
 * Place one command in the auto-register section.
 * `var` must be a unique static identifier in the translation unit.
 */
#define PM_METAL_SHELL_CMD(var, name_str, help_str, fn_)                    \
	static const pm_metal_shell_cmd_t var##_cmd = {                     \
		(name_str), (help_str), (fn_)                               \
	};                                                                  \
	static const pm_metal_shell_cmd_table_t var                         \
		__attribute__((used, section(".pm_metal_shell_cmds.1"),     \
			       aligned(16))) = { &var##_cmd, 1u }

/**
 * Multi-command table. Use with PM_METAL_SHELL_CMDS_END:
 *   PM_METAL_SHELL_CMDS(g_core) = { { "help", "...", fn }, ... };
 *   PM_METAL_SHELL_CMDS_END(g_core);
 */
#define PM_METAL_SHELL_CMDS(arr)                                            \
	static const pm_metal_shell_cmd_t arr##_cmds[]

#define PM_METAL_SHELL_CMDS_END(arr)                                        \
	static const pm_metal_shell_cmd_table_t arr                         \
		__attribute__((used, section(".pm_metal_shell_cmds.1"),     \
			       aligned(16))) = {                            \
			arr##_cmds,                                         \
			(unsigned)(sizeof (arr##_cmds) /                    \
				   sizeof (arr##_cmds[0]))                  \
		}

/** Register one top-level command (rarely needed). */
void pm_metal_shell_cmd_register(const pm_metal_shell_cmd_t *cmd);

/** Parse line into argv and dispatch to a registered handler. */
void pm_metal_shell_cmd_dispatch(const char *line);

/** Gather linker-section command tables (called from pm_metal_shell_init). */
void pm_metal_shell_cmds_install(void);

/** Print registered command help lines. */
void pm_metal_shell_cmd_help(void);

/** Shell output helpers for command handlers. */
void pm_metal_shell_out(const char *line);
void pm_metal_shell_out_lines(const char *text);
void pm_metal_shell_mark_full(void);

/** Request host exit; reboot=1 for `exit -r`. */
void pm_metal_shell_cmd_exit(int reboot);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_SHELL_CMD_H_ */
