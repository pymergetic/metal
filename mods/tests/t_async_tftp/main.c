/*
 * Guest proof — DHCP boot fields + TFTP GET (QEMU user tftp=/bootfile=).
 *
 * Empty host/path → next-server (siaddr / opt 66) + boot file (opt 67).
 */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/dev/net/tftp.h"
#include "pymergetic/metal/shell/shell/shell.h"

#define TFTP_MARK "metal-async-tftp"

typedef struct {
	uint32_t step;
	uint32_t aw;
	uint8_t  buf[128];
} guest_state_t;

int32_t
pm_metal_guest_step(int32_t self_h)
{
	guest_state_t *s;
	uint32_t       st;
	uint32_t       n;
	uint32_t       i;

	s = (guest_state_t *)(uintptr_t)pm_metal_async_coro_state(
		(pm_metal_async_handle_t)self_h);
	if (s == NULL) {
		return PM_METAL_ERROR;
	}

	switch (s->step) {
	case 0:
		for (i = 0; i < sizeof(s->buf); i++) {
			s->buf[i] = 0;
		}
		/* "" / "" → DHCP next-server + bootfile (see tftp.h). */
		s->aw = pm_metal_net_tftp_get(
			"",
			"",
			PM_METAL_NET_TFTP_IO_PTR(s->buf),
			(uint32_t)(sizeof(s->buf) - 1u));
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 1;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 1:
		st = pm_metal_net_tftp_status(s->aw);
		n  = pm_metal_net_tftp_len(s->aw);
		if (st != 0u) {
			return PM_METAL_ERROR;
		}
		if (n < (uint32_t)(sizeof(TFTP_MARK) - 1u)) {
			return PM_METAL_ERROR;
		}
		for (i = 0; TFTP_MARK[i] != '\0'; i++) {
			if (s->buf[i] != (uint8_t)TFTP_MARK[i]) {
				return PM_METAL_ERROR;
			}
		}
		pm_metal_shell_log("metal-async: tftp ok");
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
