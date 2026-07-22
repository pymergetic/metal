/*
 * Guest async proof — stackless pm_metal_guest_step + host sleep resume.
 */
#include <stddef.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/shell/shell/shell.h"

typedef struct {
	uint32_t step;
	uint32_t aw;
} guest_state_t;

int32_t
pm_metal_guest_step(int32_t self_h)
{
	guest_state_t *s;

	s = (guest_state_t *)(uintptr_t)pm_metal_async_coro_state(
		(pm_metal_async_handle_t)self_h);
	if (s == NULL) {
		return PM_METAL_ERROR;
	}

	switch (s->step) {
	case 0:
		s->aw = pm_metal_async_sleep(50);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 1;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 1:
		pm_metal_shell_log("metal-async: sleep ok");
		return PM_METAL_DONE;

	default:
		return PM_METAL_ERROR;
	}
}

/* Sync entry unused when host detects pm_metal_guest_step. */
int
main(void)
{
	return 0;
}
