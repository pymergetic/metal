/*
 * Guest async FS proof — await size + read + write on ESP.
 */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/async/async.h"
#include "pymergetic/metal/fs/fs.h"
#include "pymergetic/metal/shell/shell.h"

#define ASYNC_FS_PATH "mods/tests/async_fs.txt"
#define ASYNC_FS_OUT "mods/tests/async_fs_out.txt"
#define ASYNC_FS_EXPECT "metal-async-fs\n"
#define ASYNC_FS_EXPECT_LEN 15u
#define ASYNC_FS_WRITE "metal-async-write\n"
#define ASYNC_FS_WRITE_LEN 18u

typedef struct {
	uint32_t step;
	uint32_t aw;
	uint32_t n;
	uint8_t  buf[32];
	uint8_t  wbuf[32];
} guest_state_t;

static int
bytes_eq(const uint8_t *buf, uint32_t n, const char *exp, uint32_t elen)
{
	uint32_t i;

	if (n != elen) {
		return 0;
	}

	for (i = 0; i < n; i++) {
		if (buf[i] != (uint8_t)exp[i]) {
			return 0;
		}
	}

	return 1;
}

int32_t
pm_metal_guest_step(int32_t self_h)
{
	guest_state_t *s;
	uint32_t       dest;
	uint32_t       i;

	s = (guest_state_t *)(uintptr_t)pm_metal_async_coro_state(
		(pm_metal_async_handle_t)self_h);
	if (s == NULL) {
		return PM_METAL_ERROR;
	}

	switch (s->step) {
	case 0:
		s->aw = pm_metal_fs_size_async(ASYNC_FS_PATH);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 1;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 1:
		s->n = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (s->n != ASYNC_FS_EXPECT_LEN) {
			return PM_METAL_ERROR;
		}
		dest = (uint32_t)(uintptr_t)s->buf;
		s->aw = pm_metal_fs_read_async(ASYNC_FS_PATH, dest, sizeof(s->buf));
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 2;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 2:
		s->n = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (!bytes_eq(s->buf, s->n, ASYNC_FS_EXPECT, ASYNC_FS_EXPECT_LEN)) {
			return PM_METAL_ERROR;
		}
		for (i = 0; i < ASYNC_FS_WRITE_LEN; i++) {
			s->wbuf[i] = (uint8_t)ASYNC_FS_WRITE[i];
		}
		dest = (uint32_t)(uintptr_t)s->wbuf;
		s->aw = pm_metal_fs_write_async(ASYNC_FS_OUT, dest, ASYNC_FS_WRITE_LEN);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 3;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 3:
		s->n = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (s->n != ASYNC_FS_WRITE_LEN) {
			return PM_METAL_ERROR;
		}
		dest = (uint32_t)(uintptr_t)s->buf;
		s->aw = pm_metal_fs_read_async(ASYNC_FS_OUT, dest, sizeof(s->buf));
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 4;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 4:
		s->n = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (!bytes_eq(s->buf, s->n, ASYNC_FS_WRITE, ASYNC_FS_WRITE_LEN)) {
			return PM_METAL_ERROR;
		}
		pm_metal_shell_log("metal-async: fs ok");
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
