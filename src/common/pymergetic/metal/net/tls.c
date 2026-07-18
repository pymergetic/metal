/*
 * Net TLS — wasi-import bridge only. Host calls inline via net/tls.h → port/tls.
 */
#include "pymergetic/metal/net/tls.h"

#include "wasm_export.h"

#include <stddef.h>

static int32_t pm_metal_net_tls_ca_file_native(wasm_exec_env_t exec_env, char *out, uint32_t cap)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_tls_ca_file(out, (size_t)cap);
}

static NativeSymbol g_pm_metal_net_tls_native_symbols[] = {
	{"pm_metal_net_tls_ca_file", (void *)pm_metal_net_tls_ca_file_native, "(*~)i", NULL},
};

int pm_metal_net_tls_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_NET_TLS_WASI_MODULE, g_pm_metal_net_tls_native_symbols,
					    sizeof(g_pm_metal_net_tls_native_symbols)
						    / sizeof(g_pm_metal_net_tls_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
