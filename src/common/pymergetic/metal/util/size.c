/*
 * pm_metal_util_size_* — impl: common (see util/size.h; wasm32 mods reach
 * this same code via this file's own wasi-style import registration at
 * the bottom, not via a second compiled copy of this file).
 */
#include "pymergetic/metal/util/size.h"

#include <inttypes.h>
#include <stdio.h>

typedef struct pm_metal_util_size_unit {
	uint64_t bytes;
	const char *suffix;
} pm_metal_util_size_unit_t;

int pm_metal_util_size_format(char *out, size_t cap, uint64_t bytes)
{
	if (!out || cap == 0) {
		return -1;
	}

	static const pm_metal_util_size_unit_t units[] = {
		{1024ULL * 1024ULL * 1024ULL * 1024ULL, " TiB"},
		{1024ULL * 1024ULL * 1024ULL, " GiB"},
		{1024ULL * 1024ULL, " MiB"},
		{1024ULL, " KiB"},
	};
	size_t i;

	for (i = 0; i < sizeof(units) / sizeof(units[0]); i++) {
		if (bytes >= units[i].bytes) {
			return snprintf(out, cap, "%" PRIu64 "%s", bytes / units[i].bytes,
					 units[i].suffix);
		}
	}

	return snprintf(out, cap, "%" PRIu64 " B", bytes);
}

int pm_metal_util_size_format_bytes(char *out, size_t cap, uint64_t bytes)
{
	char human[PM_METAL_UTIL_SIZE_FORMAT_MAX];

	if (!out || cap == 0) {
		return -1;
	}
	if (pm_metal_util_size_format(human, sizeof(human), bytes) < 0) {
		return -1;
	}

	return snprintf(out, cap, "%" PRIu64 " (%s)", bytes, human);
}

/*
 * wasi-style import bridge — one module, one bridge, registered under
 * this module's own PM_METAL_UTIL_SIZE_WASI_MODULE (size.h). Wrapper C
 * signatures below are exactly what WAMR's signature string promises to
 * hand them (see wasm_runtime_register_natives()'s doc comment in
 * wasm_export.h for the letter meanings) — '*'+'~' params arrive already
 * translated to a real, bounds-checked native pointer, no manual address
 * translation needed here.
 */
#include "wasm_export.h"

static int32_t pm_metal_util_size_format_native(wasm_exec_env_t exec_env, char *out, uint32_t cap,
						 uint64_t bytes)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_size_format(out, (size_t)cap, bytes);
}

static int32_t pm_metal_util_size_format_bytes_native(wasm_exec_env_t exec_env, char *out, uint32_t cap,
							uint64_t bytes)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_size_format_bytes(out, (size_t)cap, bytes);
}

static NativeSymbol g_pm_metal_util_size_native_symbols[] = {
	{"pm_metal_util_size_format", (void *)pm_metal_util_size_format_native, "(*~I)i", NULL},
	{"pm_metal_util_size_format_bytes", (void *)pm_metal_util_size_format_bytes_native, "(*~I)i", NULL},
};

int pm_metal_util_size_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_UTIL_SIZE_WASI_MODULE, g_pm_metal_util_size_native_symbols,
					    sizeof(g_pm_metal_util_size_native_symbols)
						    / sizeof(g_pm_metal_util_size_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
