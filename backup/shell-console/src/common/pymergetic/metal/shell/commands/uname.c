/*
 * Shell builtin — `uname`. See uname.h.
 */
#include "pymergetic/metal/shell/commands/uname.h"

#include "pymergetic/metal/port/platform.h"
#include "pymergetic/metal/util/log.h"

/* Compile-time only — this is "what CPU arch was this binary compiled
 * for", never a runtime probe, so it needs no port primitive: the same
 * predefined macro set every target's own toolchain already exposes. */
#if defined(__x86_64__) || defined(_M_X64)
#define PM_METAL_UNAME_MACHINE "x86_64"
#elif defined(__aarch64__)
#define PM_METAL_UNAME_MACHINE "aarch64"
#elif defined(__arm__)
#define PM_METAL_UNAME_MACHINE "arm"
#elif defined(__riscv) && (__riscv_xlen == 64)
#define PM_METAL_UNAME_MACHINE "riscv64"
#elif defined(__riscv)
#define PM_METAL_UNAME_MACHINE "riscv32"
#elif defined(__i386__) || defined(_M_IX86)
#define PM_METAL_UNAME_MACHINE "i386"
#else
#define PM_METAL_UNAME_MACHINE "unknown"
#endif

static const char *pm_metal_shell_uname_target_name(void)
{
	switch (pm_metal_port_target_id()) {
	case PM_METAL_PORT_TARGET_LINUX:
		return "linux";
	case PM_METAL_PORT_TARGET_ZEPHYR:
		return "zephyr";
	case PM_METAL_PORT_TARGET_RUMP:
		return "rump";
	case PM_METAL_PORT_TARGET_UNIKRAFT:
		return "unikraft";
	case PM_METAL_PORT_TARGET_UNKNOWN:
	default:
		return "unknown";
	}
}

int pm_metal_shell_cmd_uname(pm_metal_shell_ctx_t *ctx, int argc, char **argv)
{
	(void)argc;
	(void)argv;
	pm_metal_util_log_write_raw(ctx->sink->out, "pm-metal %s %s", pm_metal_shell_uname_target_name(),
				     PM_METAL_UNAME_MACHINE);
	return 0;
}
