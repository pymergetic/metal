/*
 * Guest proof — Metal net: DNS + TCP connect/send/recv (QEMU SLIRP).
 */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/dev/net/net.h"
#include "pymergetic/metal/shell/shell/shell.h"

#define NET_ECHO_HOST "10.0.2.2"
#define NET_ECHO_PORT 10007u

typedef struct {
	uint32_t step;
	uint32_t aw;
	uint32_t sock;
	uint8_t  buf[64];
} guest_state_t;

static uint32_t
net_result(pm_metal_async_handle_t self_h)
{
	return pm_metal_async_result_u32(self_h);
}

int32_t
pm_metal_guest_step(int32_t self_h)
{
	guest_state_t *s;
	uint32_t       n;

	s = (guest_state_t *)(uintptr_t)pm_metal_async_coro_state(
		(pm_metal_async_handle_t)self_h);
	if (s == NULL) {
		return PM_METAL_ERROR;
	}

	switch (s->step) {
	case 0:
		s->aw = pm_metal_net_dns(NET_ECHO_HOST);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 1;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 1:
		if (net_result((pm_metal_async_handle_t)self_h) == 0) {
			return PM_METAL_ERROR;
		}
		s->sock = pm_metal_net_socket(PM_METAL_NET_AF_INET,
					      PM_METAL_NET_SOCK_STREAM);
		if (s->sock == PM_METAL_NET_SOCK_INVALID) {
			return PM_METAL_ERROR;
		}
		s->aw = pm_metal_net_connect(s->sock, NET_ECHO_HOST, NET_ECHO_PORT);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 2;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 2:
		if (net_result((pm_metal_async_handle_t)self_h) == 0) {
			return PM_METAL_ERROR;
		}
		s->buf[0] = 'P';
		s->buf[1] = 'I';
		s->buf[2] = 'N';
		s->buf[3] = '\n';
		n = pm_metal_net_send(s->sock, PM_METAL_NET_IO_PTR(s->buf), 4);
		if (n != 4) {
			return PM_METAL_ERROR;
		}
		s->aw = pm_metal_net_recv(s->sock, PM_METAL_NET_IO_PTR(s->buf),
					  sizeof(s->buf));
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 3;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 3:
		n = net_result((pm_metal_async_handle_t)self_h);
		if (n < 3) {
			return PM_METAL_ERROR;
		}
		if (s->buf[0] != 'P' || s->buf[1] != 'I' || s->buf[2] != 'N') {
			return PM_METAL_ERROR;
		}
		pm_metal_net_close(s->sock);
		pm_metal_shell_log("metal-async: net ok");
		return PM_METAL_DONE;

	default:
		return PM_METAL_ERROR;
	}
}

int
main(void)
{
	return 0;
}
