/*
 * Net TCP — wasi-import bridge only. Host calls inline via net/tcp.h → port/tcp.
 */
#include "pymergetic/metal/net/tcp.h"

#include "wasm_export.h"

#include <stddef.h>

static int32_t pm_metal_net_tcp_open_native(wasm_exec_env_t exec_env, uint32_t family)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_tcp_open((uint8_t)family);
}

static int32_t pm_metal_net_tcp_close_native(wasm_exec_env_t exec_env, int32_t fd)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_tcp_close((int)fd);
}

static int32_t pm_metal_net_tcp_set_timeout_ms_native(wasm_exec_env_t exec_env, int32_t fd,
						       int32_t timeout_ms)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_tcp_set_timeout_ms((int)fd, timeout_ms);
}

static int32_t pm_metal_net_tcp_bind_native(wasm_exec_env_t exec_env, int32_t fd,
					     const pm_metal_net_addr_t *addr)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_tcp_bind((int)fd, addr);
}

static int32_t pm_metal_net_tcp_listen_native(wasm_exec_env_t exec_env, int32_t fd, int32_t backlog)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_tcp_listen((int)fd, (int)backlog);
}

static int32_t pm_metal_net_tcp_accept_native(wasm_exec_env_t exec_env, int32_t fd,
					       pm_metal_net_addr_t *out_peer)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_tcp_accept((int)fd, out_peer);
}

static int32_t pm_metal_net_tcp_connect_native(wasm_exec_env_t exec_env, int32_t fd,
						const pm_metal_net_addr_t *to)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_tcp_connect((int)fd, to);
}

static int32_t pm_metal_net_tcp_send_native(wasm_exec_env_t exec_env, int32_t fd, const void *buf,
					     uint32_t len, uint32_t *out_sent)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_tcp_send((int)fd, buf, len, out_sent);
}

static int32_t pm_metal_net_tcp_recv_native(wasm_exec_env_t exec_env, int32_t fd, void *buf, uint32_t cap,
					     uint32_t *out_len)
{
	(void)exec_env;
	return (int32_t)pm_metal_net_tcp_recv((int)fd, buf, cap, out_len);
}

static NativeSymbol g_pm_metal_net_tcp_native_symbols[] = {
	{"pm_metal_net_tcp_open", (void *)pm_metal_net_tcp_open_native, "(i)i", NULL},
	{"pm_metal_net_tcp_close", (void *)pm_metal_net_tcp_close_native, "(i)i", NULL},
	{"pm_metal_net_tcp_set_timeout_ms", (void *)pm_metal_net_tcp_set_timeout_ms_native, "(ii)i", NULL},
	{"pm_metal_net_tcp_bind", (void *)pm_metal_net_tcp_bind_native, "(i*)i", NULL},
	{"pm_metal_net_tcp_listen", (void *)pm_metal_net_tcp_listen_native, "(ii)i", NULL},
	{"pm_metal_net_tcp_accept", (void *)pm_metal_net_tcp_accept_native, "(i*)i", NULL},
	{"pm_metal_net_tcp_connect", (void *)pm_metal_net_tcp_connect_native, "(i*)i", NULL},
	{"pm_metal_net_tcp_send", (void *)pm_metal_net_tcp_send_native, "(i*~*)i", NULL},
	{"pm_metal_net_tcp_recv", (void *)pm_metal_net_tcp_recv_native, "(i*~*)i", NULL},
};

int pm_metal_net_tcp_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_NET_TCP_WASI_MODULE, g_pm_metal_net_tcp_native_symbols,
					    sizeof(g_pm_metal_net_tcp_native_symbols)
						    / sizeof(g_pm_metal_net_tcp_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
