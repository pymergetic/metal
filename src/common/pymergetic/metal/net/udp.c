/*
 * Net UDP — wasi-import bridge only. Host calls inline via net/udp.h → port/udp.
 */
#include "pymergetic/metal/net/udp.h"

#include "wasm_export.h"

#include <stddef.h>

static int32_t pm_metal_net_udp_open_native(wasm_exec_env_t exec_env, uint32_t family)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_udp_open((uint8_t)family);
}

static int32_t pm_metal_net_udp_close_native(wasm_exec_env_t exec_env, int32_t fd)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_udp_close((int)fd);
}

static int32_t pm_metal_net_udp_set_timeout_ms_native(wasm_exec_env_t exec_env, int32_t fd,
						       int32_t timeout_ms)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_udp_set_timeout_ms((int)fd, timeout_ms);
}

static int32_t pm_metal_net_udp_sendto_native(wasm_exec_env_t exec_env, int32_t fd, const void *buf,
					       uint32_t len, const pm_metal_net_addr_t *to)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_udp_sendto((int)fd, buf, len, to);
}

static int32_t pm_metal_net_udp_recv_native(wasm_exec_env_t exec_env, int32_t fd, void *buf, uint32_t cap,
					     uint32_t *out_len)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_udp_recv((int)fd, buf, cap, out_len);
}

static NativeSymbol g_pm_metal_net_udp_native_symbols[] = {
	{"pm_metal_net_udp_open", (void *)pm_metal_net_udp_open_native, "(i)i", NULL},
	{"pm_metal_net_udp_close", (void *)pm_metal_net_udp_close_native, "(i)i", NULL},
	{"pm_metal_net_udp_set_timeout_ms", (void *)pm_metal_net_udp_set_timeout_ms_native, "(ii)i", NULL},
	{"pm_metal_net_udp_sendto", (void *)pm_metal_net_udp_sendto_native, "(i*~*)i", NULL},
	{"pm_metal_net_udp_recv", (void *)pm_metal_net_udp_recv_native, "(i*~*)i", NULL},
};

int pm_metal_net_udp_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_NET_UDP_WASI_MODULE, g_pm_metal_net_udp_native_symbols,
					    sizeof(g_pm_metal_net_udp_native_symbols)
						    / sizeof(g_pm_metal_net_udp_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
