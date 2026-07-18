/*
 * Net DNS — wasi-import bridge only. Host calls inline via net/dns.h → port/dns.
 */
#include "pymergetic/metal/net/dns.h"

#include "wasm_export.h"

#include <stddef.h>

static int32_t pm_metal_net_dns_lookup_native(wasm_exec_env_t exec_env, const char *host, uint32_t port,
					       pm_metal_net_addr_t *out, uint32_t out_cap, uint32_t *out_n)
{
	size_t n = 0;
	int rc;

	(void)exec_env;
	rc = pm_metal_net_dns_lookup(host, (uint16_t)port, out, (size_t)out_cap, &n);
	if (out_n) {
		*out_n = (uint32_t)n;
	}
	return (int32_t)rc;
}

static NativeSymbol g_pm_metal_net_dns_native_symbols[] = {
	{"pm_metal_net_dns_lookup", (void *)pm_metal_net_dns_lookup_native, "($i*i*)i", NULL},
};

int pm_metal_net_dns_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_NET_DNS_WASI_MODULE, g_pm_metal_net_dns_native_symbols,
					    sizeof(g_pm_metal_net_dns_native_symbols)
						    / sizeof(g_pm_metal_net_dns_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
