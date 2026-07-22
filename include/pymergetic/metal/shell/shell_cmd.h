/*
 * Metal shell — registered host commands.
 *
 * Topic modules define commands beside their domain code and register via
 * pm_metal_shell_cmds_register_*() from pm_metal_shell_cmds_install().
 *
 * impl: common — src/pymergetic/metal/shell/shell/shell_cmd.c
 */
#ifndef PYMERGETIC_METAL_SHELL_SHELL_CMD_H_
#define PYMERGETIC_METAL_SHELL_SHELL_CMD_H_

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef void (*pm_metal_shell_cmd_fn)(const char *arg);

typedef struct pm_metal_shell_cmd {
	const char *name;
	const char *help;
	pm_metal_shell_cmd_fn fn;
} pm_metal_shell_cmd_t;

/** Register one top-level command (e.g. "net", "hwinfo"). */
void pm_metal_shell_cmd_register(const pm_metal_shell_cmd_t *cmd);

/** Parse "cmd [arg...]" and dispatch to a registered handler. */
void pm_metal_shell_cmd_dispatch(const char *line);

/** Register all topic command tables (called from pm_metal_shell_init). */
void pm_metal_shell_cmds_install(void);

/** Print registered command help lines. */
void pm_metal_shell_cmd_help(void);

/** Shell output helpers for command handlers. */
void pm_metal_shell_out(const char *line);
void pm_metal_shell_out_lines(const char *text);
void pm_metal_shell_mark_full(void);

/** Request host exit; reboot=1 for `exit -r`. */
void pm_metal_shell_cmd_exit(int reboot);

void pm_metal_shell_cmds_register_core(void);
void pm_metal_shell_cmds_register_net(void);
void pm_metal_shell_cmds_register_hwinfo(void);
void pm_metal_shell_cmds_register_input(void);

#endif /* !__wasm__ */

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_SHELL_SHELL_CMD_H_ */
