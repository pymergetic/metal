/*
 * Guest async blk proof — ready/capacity + awaitable LBA0 read.
 */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/dev/blk/blk.h"
#include "pymergetic/metal/shell/shell/shell.h"

typedef struct {
	uint32_t step;
	uint32_t aw;
	uint32_t h;
	uint8_t  sec[512];
} guest_state_t;

int32_t
pm_metal_guest_step(int32_t self_h)
{
	guest_state_t *s;
	uint32_t       dest;
	uint32_t       n;
	uint64_t       cap;

	s = (guest_state_t *)(uintptr_t)pm_metal_async_coro_state(
		(pm_metal_async_handle_t)self_h);
	if (s == NULL) {
		return PM_METAL_ERROR;
	}

	switch (s->step) {
	case 0:
		if (pm_metal_blk_count() == 0) {
			return PM_METAL_ERROR;
		}
		s->h = pm_metal_blk_at(0);
		if (s->h == PM_METAL_BLK_INVALID || !pm_metal_blk_ready(s->h)) {
			return PM_METAL_ERROR;
		}
		cap = pm_metal_blk_capacity_sectors(s->h);
		if (cap == 0) {
			return PM_METAL_ERROR;
		}
		dest = (uint32_t)(uintptr_t)s->sec;
		s->aw = pm_metal_blk_read_async(s->h, 0, dest, 1);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 1;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 1:
		n = pm_metal_blk_result((pm_metal_async_handle_t)self_h);
		if (n != 1) {
			return PM_METAL_ERROR;
		}
		pm_metal_shell_log("metal-async: blk ok");
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
