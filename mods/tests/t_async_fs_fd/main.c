/*
 * Guest fd-shaped FS proof — open/lseek/fread/fpread/close + stat/readdir.
 */
#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/fs/fs.h"
#include "pymergetic/metal/shell/shell/shell.h"

#define FS_PATH "mods/tests/async_fs.txt"
#define FS_DIR  "mods/tests"
#define FS_EXPECT "metal-async-fs\n"
#define FS_EXPECT_LEN 15u

typedef struct {
	uint32_t step;
	uint32_t aw;
	uint32_t n;
	pm_metal_fs_h fh;
	pm_metal_fs_stat_t st;
	uint8_t buf[32];
	uint8_t name[64];
	uint32_t found;
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

	s = (guest_state_t *)(uintptr_t)pm_metal_async_coro_state(
		(pm_metal_async_handle_t)self_h);
	if (s == NULL) {
		return PM_METAL_ERROR;
	}

	switch (s->step) {
	case 0:
		dest = (uint32_t)(uintptr_t)&s->st;
		s->aw = pm_metal_fs_stat_async(FS_PATH, dest);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 1;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 1:
		s->n = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (s->n != 1u || s->st.size != FS_EXPECT_LEN
		    || s->st.type != PM_METAL_FS_TYPE_FILE) {
			return PM_METAL_ERROR;
		}
		s->aw = pm_metal_fs_open_async(FS_PATH, PM_METAL_FS_O_RDONLY);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 2;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 2:
		s->fh = (pm_metal_fs_h)pm_metal_fs_result(
			(pm_metal_async_handle_t)self_h);
		if (s->fh == PM_METAL_FS_INVALID) {
			return PM_METAL_ERROR;
		}
		if (pm_metal_fs_lseek(s->fh, 0, PM_METAL_FS_SEEK_SET) != 0) {
			return PM_METAL_ERROR;
		}
		dest = (uint32_t)(uintptr_t)s->buf;
		s->aw = pm_metal_fs_fread_async(s->fh, dest, sizeof(s->buf));
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 3;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 3:
		s->n = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (!bytes_eq(s->buf, s->n, FS_EXPECT, FS_EXPECT_LEN)) {
			return PM_METAL_ERROR;
		}
		dest = (uint32_t)(uintptr_t)s->buf;
		s->aw = pm_metal_fs_fpread_async(s->fh, 6u, dest, sizeof(s->buf));
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 4;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 4:
		s->n = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (!bytes_eq(s->buf, s->n, "async-fs\n", 9u)) {
			return PM_METAL_ERROR;
		}
		s->aw = pm_metal_fs_close_async(s->fh);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 5;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 5:
		s->n = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (s->n != 1u) {
			return PM_METAL_ERROR;
		}
		s->aw = pm_metal_fs_open_async(FS_DIR, PM_METAL_FS_O_RDONLY
					       | PM_METAL_FS_O_DIRECTORY);
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 6;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 6:
		s->fh = (pm_metal_fs_h)pm_metal_fs_result(
			(pm_metal_async_handle_t)self_h);
		if (s->fh == PM_METAL_FS_INVALID) {
			return PM_METAL_ERROR;
		}
		s->found = 0;
		s->step  = 7;
		return PM_METAL_PENDING;

	case 7:
		dest = (uint32_t)(uintptr_t)s->name;
		s->aw = pm_metal_fs_readdir_async(s->fh, dest, sizeof(s->name));
		if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
			return PM_METAL_ERROR;
		}
		s->step = 8;
		return pm_metal_async_await((pm_metal_async_handle_t)self_h, s->aw);

	case 8:
		s->n = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (s->n == 0u) {
			if (s->found == 0u) {
				return PM_METAL_ERROR;
			}
			s->aw = pm_metal_fs_close_async(s->fh);
			if (s->aw == PM_METAL_ASYNC_HANDLE_INVALID) {
				return PM_METAL_ERROR;
			}
			s->step = 9;
			return pm_metal_async_await((pm_metal_async_handle_t)self_h,
						    s->aw);
		}
		if (s->n >= sizeof(s->name)) {
			return PM_METAL_ERROR;
		}
		s->name[s->n] = '\0';
		if (bytes_eq(s->name, s->n, "async_fs.txt", 12u)) {
			s->found = 1u;
		}
		s->step = 7;
		return PM_METAL_PENDING;

	case 9:
		s->n = pm_metal_fs_result((pm_metal_async_handle_t)self_h);
		if (s->n != 1u) {
			return PM_METAL_ERROR;
		}
		pm_metal_shell_log("metal-async: fs-fd ok");
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
