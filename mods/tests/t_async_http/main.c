/*
 * Guest proof — HTTP GET over Metal net (HTTPS example.com).
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/dev/net/http.h"
#include "pymergetic/metal/dev/net/net.h"
#include "pymergetic/metal/shell/shell/shell.h"

#define HTTP_TEST_URL "https://example.com/"

typedef struct {
	uint32_t step;
	uint32_t aw;
	char     body[512];
} guest_state_t;

int32_t
pm_metal_guest_step(int32_t self_h)
{
	guest_state_t *s;
	uint32_t       st;
	uint32_t       n;

	s = (guest_state_t *)(uintptr_t)pm_metal_async_coro_state(
		(pm_metal_async_handle_t)self_h);
	if (s == NULL) {
		return PM_METAL_ERROR;
	}

	switch (s->step) {
	case 0:
		memset(s->body, 0, sizeof(s->body));
		s->aw = pm_metal_net_http_get(HTTP_TEST_URL,
					      PM_METAL_NET_IO_PTR(s->body),
					      (uint32_t)(sizeof(s->body) - 1));
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 1;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 1:
		st = pm_metal_net_http_status(s->aw);
		n  = pm_metal_net_http_body_len(s->aw);
		if (st != 200u) {
			return PM_METAL_ERROR;
		}
		if (n == 0u) {
			return PM_METAL_ERROR;
		}
		if (strstr(s->body, "Example Domain") == NULL) {
			return PM_METAL_ERROR;
		}
		pm_metal_shell_log("metal-async: http ok");
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
