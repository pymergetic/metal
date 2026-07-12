/*
 * pm_metal_util_log_* bodies. #include this from exactly one loader .c per
 * binary — never from more than one TU, and never as a substitute for
 * log.h in ordinary callers (they only need the contract, not the body).
 */
#ifndef PYMERGETIC_METAL_UTIL_LOG_IMPL_H_
#define PYMERGETIC_METAL_UTIL_LOG_IMPL_H_

#include "pymergetic/metal/util/log.h"

#include <stdarg.h>

static pm_metal_log_level_t g_pm_metal_util_log_level = PM_METAL_LOG_INFO;

void pm_metal_util_log_set_level(pm_metal_log_level_t level)
{
	g_pm_metal_util_log_level = level;
}

pm_metal_log_level_t pm_metal_util_log_get_level(void)
{
	return g_pm_metal_util_log_level;
}

const char *pm_metal_util_log_level_name(pm_metal_log_level_t level)
{
	static const char *names[PM_METAL_LOG_LEVEL_COUNT] = {
		"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL",
	};

	if ((int)level < 0 || level >= PM_METAL_LOG_LEVEL_COUNT) {
		return "?";
	}
	return names[level];
}

void pm_metal_util_log_write(FILE *out, pm_metal_log_level_t level, const char *fmt, ...)
{
	if (!out || level < g_pm_metal_util_log_level) {
		return;
	}

	va_list ap;

	fprintf(out, "[%s] ", pm_metal_util_log_level_name(level));
	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fputc('\n', out);
	fflush(out);
}

void pm_metal_util_log_write_raw(FILE *out, const char *fmt, ...)
{
	if (!out) {
		return;
	}

	va_list ap;

	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fputc('\n', out);
	fflush(out);
}

#endif /* PYMERGETIC_METAL_UTIL_LOG_IMPL_H_ */
