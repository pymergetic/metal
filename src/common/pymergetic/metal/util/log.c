/*
 * pm_metal_util_log_* — impl: common (see util/log.h; wasm32 mods reach
 * this same code via this file's own wasi-style import registration at
 * the bottom, not via a second compiled copy of this file). *f()
 * convenience wrappers are host-only (see log.h) and stay in this file
 * too since they're plain calls into write()/write_raw() below, nothing
 * native-specific about them.
 */
#include "pymergetic/metal/util/log.h"

#include <stdio.h>

static pm_metal_log_level_t g_pm_metal_util_log_level = PM_METAL_LOG_INFO;

static FILE *pm_metal_util_log_stream_file(pm_metal_log_stream_t stream)
{
	return stream == PM_METAL_LOG_STREAM_STDERR ? stderr : stdout;
}

void pm_metal_util_log_set_level(pm_metal_log_level_t level)
{
	g_pm_metal_util_log_level = level;
}

pm_metal_log_level_t pm_metal_util_log_get_level(void)
{
	return g_pm_metal_util_log_level;
}

int pm_metal_util_log_level_name(pm_metal_log_level_t level, char *out, size_t cap)
{
	static const char *names[PM_METAL_LOG_LEVEL_COUNT] = {
		"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL",
	};

	if (!out || cap == 0) {
		return -1;
	}

	const char *name = ((int)level < 0 || level >= PM_METAL_LOG_LEVEL_COUNT) ? "?" : names[level];

	return snprintf(out, cap, "%s", name);
}

void pm_metal_util_log_write(pm_metal_log_stream_t stream, pm_metal_log_level_t level, const char *msg)
{
	if (!msg || level < g_pm_metal_util_log_level) {
		return;
	}

	FILE *out = pm_metal_util_log_stream_file(stream);
	char name[8]; /* "TRACE".."FATAL" + NUL, "?" for out-of-range */

	pm_metal_util_log_level_name(level, name, sizeof(name));
	fprintf(out, "[%s] %s\n", name, msg);
	fflush(out);
}

void pm_metal_util_log_write_raw(pm_metal_log_stream_t stream, const char *msg)
{
	if (!msg) {
		return;
	}

	FILE *out = pm_metal_util_log_stream_file(stream);

	fprintf(out, "%s\n", msg);
	fflush(out);
}

#if !defined(__wasm__)
#include <stdarg.h>

void pm_metal_util_log_writef(pm_metal_log_stream_t stream, pm_metal_log_level_t level, const char *fmt, ...)
{
	if (!fmt || level < g_pm_metal_util_log_level) {
		return;
	}

	char msg[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	pm_metal_util_log_write(stream, level, msg);
}

void pm_metal_util_log_write_rawf(pm_metal_log_stream_t stream, const char *fmt, ...)
{
	if (!fmt) {
		return;
	}

	char msg[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, sizeof(msg), fmt, ap);
	va_end(ap);

	pm_metal_util_log_write_raw(stream, msg);
}
#endif

/*
 * wasi-style import bridge — see size.c's own bridge comment for the
 * general signature-string rules this follows.
 */
#include "wasm_export.h"

static void pm_metal_util_log_set_level_native(wasm_exec_env_t exec_env, int32_t level)
{
	(void)exec_env;
	pm_metal_util_log_set_level((pm_metal_log_level_t)level);
}

static int32_t pm_metal_util_log_get_level_native(wasm_exec_env_t exec_env)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_log_get_level();
}

static int32_t pm_metal_util_log_level_name_native(wasm_exec_env_t exec_env, int32_t level, char *out,
						     uint32_t cap)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_log_level_name((pm_metal_log_level_t)level, out, (size_t)cap);
}

static void pm_metal_util_log_write_native(wasm_exec_env_t exec_env, int32_t stream, int32_t level,
					    const char *msg)
{
	(void)exec_env;
	pm_metal_util_log_write((pm_metal_log_stream_t)stream, (pm_metal_log_level_t)level, msg);
}

static void pm_metal_util_log_write_raw_native(wasm_exec_env_t exec_env, int32_t stream, const char *msg)
{
	(void)exec_env;
	pm_metal_util_log_write_raw((pm_metal_log_stream_t)stream, msg);
}

static NativeSymbol g_pm_metal_util_log_native_symbols[] = {
	{"pm_metal_util_log_set_level", (void *)pm_metal_util_log_set_level_native, "(i)", NULL},
	{"pm_metal_util_log_get_level", (void *)pm_metal_util_log_get_level_native, "()i", NULL},
	{"pm_metal_util_log_level_name", (void *)pm_metal_util_log_level_name_native, "(i*~)i", NULL},
	{"pm_metal_util_log_write", (void *)pm_metal_util_log_write_native, "(ii$)", NULL},
	{"pm_metal_util_log_write_raw", (void *)pm_metal_util_log_write_raw_native, "(i$)", NULL},
};

int pm_metal_util_log_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_UTIL_LOG_WASI_MODULE, g_pm_metal_util_log_native_symbols,
					    sizeof(g_pm_metal_util_log_native_symbols)
						    / sizeof(g_pm_metal_util_log_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
