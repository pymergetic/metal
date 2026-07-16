/*
 * pm_metal_util_tar_* — impl: common (see util/tar.h).
 *
 * Full tar state (incl. mtar_t function pointers) lives in a host-only
 * slot table. Guest-facing iter_t/writer_t hold only an opaque host id.
 */
#include "pymergetic/metal/util/tar.h"

#include <limits.h>
#include <string.h>

#include "external/microtar/src/microtar.h"

#define PM_METAL_UTIL_TAR_MAX_SLOTS 32

typedef struct {
	void *base;
	size_t limit;
} pm_metal_util_tar_mem_t;

typedef struct {
	mtar_t tar;
	pm_metal_util_tar_mem_t mem;
	mtar_header_t hdr;
	size_t data_read;
	int have_entry;
} pm_metal_util_tar_state_t;

typedef struct {
	int used;
	uint32_t gen;
	pm_metal_util_tar_state_t state;
} pm_metal_util_tar_slot_t;

static pm_metal_util_tar_slot_t g_pm_metal_util_tar_slots[PM_METAL_UTIL_TAR_MAX_SLOTS];

static pm_metal_util_tar_slot_t *pm_metal_util_tar_slot_claim(void)
{
	int i;

	for (i = 0; i < PM_METAL_UTIL_TAR_MAX_SLOTS; i++) {
		pm_metal_util_tar_slot_t *s = &g_pm_metal_util_tar_slots[i];

		if (!s->used) {
			s->used = 1;
			s->gen++;
			if (s->gen == 0) {
				s->gen = 1;
			}
			memset(&s->state, 0, sizeof(s->state));
			return s;
		}
	}
	return NULL;
}

static void pm_metal_util_tar_slot_release(pm_metal_util_tar_slot_t *s)
{
	if (!s || !s->used) {
		return;
	}
	s->used = 0;
	memset(&s->state, 0, sizeof(s->state));
}

static uint32_t pm_metal_util_tar_handle_of(const pm_metal_util_tar_slot_t *s)
{
	int idx;

	if (!s || !s->used) {
		return 0;
	}
	idx = (int)(s - g_pm_metal_util_tar_slots);
	return (s->gen << 16) | (uint32_t)(idx + 1);
}

static pm_metal_util_tar_slot_t *pm_metal_util_tar_slot_from_handle(uint32_t handle)
{
	uint32_t idx;
	uint32_t gen;
	pm_metal_util_tar_slot_t *s;

	if (handle == 0) {
		return NULL;
	}
	idx = (handle & 0xffffu) - 1u;
	gen = handle >> 16;
	if (idx >= PM_METAL_UTIL_TAR_MAX_SLOTS) {
		return NULL;
	}
	s = &g_pm_metal_util_tar_slots[idx];
	if (!s->used || s->gen != gen) {
		return NULL;
	}
	return s;
}

static pm_metal_util_tar_state_t *pm_metal_util_tar_state(const pm_metal_util_tar_iter_t *it)
{
	pm_metal_util_tar_slot_t *s;

	if (!it) {
		return NULL;
	}
	s = pm_metal_util_tar_slot_from_handle(it->id);
	return s ? &s->state : NULL;
}

static pm_metal_util_tar_state_t *pm_metal_util_tar_writer_state(const pm_metal_util_tar_writer_t *w)
{
	pm_metal_util_tar_slot_t *s;

	if (!w) {
		return NULL;
	}
	s = pm_metal_util_tar_slot_from_handle(w->id);
	return s ? &s->state : NULL;
}

static void pm_metal_util_tar_release_handle(uint32_t handle)
{
	pm_metal_util_tar_slot_release(pm_metal_util_tar_slot_from_handle(handle));
}

static int pm_metal_util_tar_mem_read(mtar_t *tar, void *data, unsigned size)
{
	pm_metal_util_tar_mem_t *mem = (pm_metal_util_tar_mem_t *)tar->stream;

	if ((size_t)tar->pos + (size_t)size > mem->limit) {
		return MTAR_EREADFAIL;
	}
	memcpy(data, (const unsigned char *)mem->base + tar->pos, size);
	return MTAR_ESUCCESS;
}

static int pm_metal_util_tar_mem_write(mtar_t *tar, const void *data, unsigned size)
{
	pm_metal_util_tar_mem_t *mem = (pm_metal_util_tar_mem_t *)tar->stream;

	if ((size_t)tar->pos + (size_t)size > mem->limit) {
		return MTAR_EWRITEFAIL;
	}
	memcpy((unsigned char *)mem->base + tar->pos, data, size);
	return MTAR_ESUCCESS;
}

static int pm_metal_util_tar_mem_seek(mtar_t *tar, unsigned pos)
{
	pm_metal_util_tar_mem_t *mem = (pm_metal_util_tar_mem_t *)tar->stream;

	return (size_t)pos <= mem->limit ? MTAR_ESUCCESS : MTAR_ESEEKFAIL;
}

static int pm_metal_util_tar_mem_close(mtar_t *tar)
{
	(void)tar;
	return MTAR_ESUCCESS;
}

void pm_metal_util_tar_iter_init(pm_metal_util_tar_iter_t *it, const void *buf, size_t len)
{
	pm_metal_util_tar_slot_t *slot;
	pm_metal_util_tar_state_t *st;

	if (!it) {
		return;
	}
	if (it->id) {
		pm_metal_util_tar_release_handle(it->id);
		it->id = 0;
	}
	slot = pm_metal_util_tar_slot_claim();
	if (!slot) {
		it->id = 0;
		return;
	}
	st = &slot->state;
	st->mem.base = (void *)buf;
	st->mem.limit = len;
	st->tar.read = pm_metal_util_tar_mem_read;
	st->tar.seek = pm_metal_util_tar_mem_seek;
	st->tar.close = pm_metal_util_tar_mem_close;
	st->tar.stream = &st->mem;
	it->id = pm_metal_util_tar_handle_of(slot);
}

int pm_metal_util_tar_iter_next(pm_metal_util_tar_iter_t *it)
{
	pm_metal_util_tar_state_t *st = pm_metal_util_tar_state(it);
	int err;

	if (!st) {
		return -1;
	}

	if (st->have_entry) {
		if (pm_metal_util_tar_mem_seek(&st->tar, st->tar.last_header) != MTAR_ESUCCESS) {
			st->have_entry = 0;
			return -1;
		}
		st->tar.pos = st->tar.last_header;
		if (mtar_next(&st->tar) != MTAR_ESUCCESS) {
			st->have_entry = 0;
			return -1;
		}
	}

	err = mtar_read_header(&st->tar, &st->hdr);
	if (err == MTAR_ENULLRECORD) {
		st->have_entry = 0;
		return 0;
	}
	if (err != MTAR_ESUCCESS) {
		st->have_entry = 0;
		return -1;
	}

	st->tar.remaining_data = 0;
	st->data_read = 0;
	st->have_entry = 1;
	return 1;
}

int pm_metal_util_tar_iter_name(const pm_metal_util_tar_iter_t *it, char *out, size_t cap)
{
	const pm_metal_util_tar_state_t *st = pm_metal_util_tar_state(it);
	size_t n;

	if (!st || !st->have_entry || !out || cap == 0) {
		return -1;
	}

	n = strnlen(st->hdr.name, sizeof(st->hdr.name));
	if (n + 1 > cap) {
		return -1;
	}
	memcpy(out, st->hdr.name, n);
	out[n] = '\0';
	return (int)n;
}

uint64_t pm_metal_util_tar_iter_size(const pm_metal_util_tar_iter_t *it)
{
	const pm_metal_util_tar_state_t *st = pm_metal_util_tar_state(it);

	return (st && st->have_entry) ? (uint64_t)st->hdr.size : 0;
}

int pm_metal_util_tar_iter_is_dir(const pm_metal_util_tar_iter_t *it)
{
	const pm_metal_util_tar_state_t *st = pm_metal_util_tar_state(it);

	if (!st || !st->have_entry) {
		return -1;
	}
	return st->hdr.type == MTAR_TDIR ? 1 : 0;
}

int pm_metal_util_tar_iter_read(pm_metal_util_tar_iter_t *it, void *dst, size_t dst_cap)
{
	pm_metal_util_tar_state_t *st = pm_metal_util_tar_state(it);
	size_t remaining, want;

	if (!st || !st->have_entry || !dst) {
		return -1;
	}
	if (st->data_read >= (size_t)st->hdr.size) {
		return 0;
	}

	remaining = (size_t)st->hdr.size - st->data_read;
	want = dst_cap < remaining ? dst_cap : remaining;
	if (want > (size_t)INT_MAX) {
		want = (size_t)INT_MAX;
	}
	if (want == 0) {
		return 0;
	}
	if (mtar_read_data(&st->tar, dst, (unsigned)want) != MTAR_ESUCCESS) {
		return -1;
	}
	st->data_read += want;
	return (int)want;
}

void pm_metal_util_tar_iter_close(pm_metal_util_tar_iter_t *it)
{
	if (!it || !it->id) {
		return;
	}
	pm_metal_util_tar_release_handle(it->id);
	it->id = 0;
}

void pm_metal_util_tar_writer_init(pm_metal_util_tar_writer_t *w, void *buf, size_t cap)
{
	pm_metal_util_tar_slot_t *slot;
	pm_metal_util_tar_state_t *st;

	if (!w) {
		return;
	}
	if (w->id) {
		pm_metal_util_tar_release_handle(w->id);
		w->id = 0;
	}
	slot = pm_metal_util_tar_slot_claim();
	if (!slot) {
		w->id = 0;
		return;
	}
	st = &slot->state;
	st->mem.base = buf;
	st->mem.limit = cap;
	st->tar.write = pm_metal_util_tar_mem_write;
	st->tar.seek = pm_metal_util_tar_mem_seek;
	st->tar.close = pm_metal_util_tar_mem_close;
	st->tar.stream = &st->mem;
	w->id = pm_metal_util_tar_handle_of(slot);
}

int pm_metal_util_tar_writer_put_header(pm_metal_util_tar_writer_t *w, const char *name, uint64_t size,
					 int is_dir)
{
	pm_metal_util_tar_state_t *st = pm_metal_util_tar_writer_state(w);
	int err;

	if (!st || !name || strlen(name) >= PM_METAL_UTIL_TAR_NAME_MAX) {
		return -1;
	}
	if (is_dir && size != 0) {
		return -1;
	}
	if (size > (uint64_t)UINT_MAX) {
		return -1;
	}
	if (st->tar.remaining_data != 0) {
		return -1;
	}

	err = is_dir ? mtar_write_dir_header(&st->tar, name)
		     : mtar_write_file_header(&st->tar, name, (unsigned)size);

	return err == MTAR_ESUCCESS ? 0 : -1;
}

int pm_metal_util_tar_writer_put_data(pm_metal_util_tar_writer_t *w, const void *src, size_t src_len)
{
	pm_metal_util_tar_state_t *st = pm_metal_util_tar_writer_state(w);

	if (!st) {
		return -1;
	}
	if (src_len == 0) {
		return 0;
	}
	if (!src || src_len > (size_t)st->tar.remaining_data || src_len > (size_t)UINT_MAX) {
		return -1;
	}

	return mtar_write_data(&st->tar, src, (unsigned)src_len) == MTAR_ESUCCESS ? 0 : -1;
}

int pm_metal_util_tar_writer_put(pm_metal_util_tar_writer_t *w, const char *name, int is_dir,
				  const void *src, size_t src_len)
{
	uint64_t size = is_dir ? 0 : (uint64_t)src_len;

	if (pm_metal_util_tar_writer_put_header(w, name, size, is_dir) != 0) {
		return -1;
	}
	if (is_dir || src_len == 0) {
		return 0;
	}
	return pm_metal_util_tar_writer_put_data(w, src, src_len);
}

int64_t pm_metal_util_tar_writer_finish(pm_metal_util_tar_writer_t *w)
{
	pm_metal_util_tar_state_t *st = pm_metal_util_tar_writer_state(w);
	int64_t pos;

	if (!st) {
		return -1;
	}
	if (st->tar.remaining_data != 0) {
		return -1;
	}
	if (mtar_finalize(&st->tar) != MTAR_ESUCCESS) {
		return -1;
	}
	pos = (int64_t)st->tar.pos;
	if (w) {
		pm_metal_util_tar_release_handle(w->id);
		w->id = 0;
	}
	return pos;
}

#include "wasm_export.h"

static void pm_metal_util_tar_iter_init_native(wasm_exec_env_t exec_env, pm_metal_util_tar_iter_t *it,
						const void *buf, uint32_t len)
{
	(void)exec_env;
	pm_metal_util_tar_iter_init(it, buf, (size_t)len);
}

static int32_t pm_metal_util_tar_iter_next_native(wasm_exec_env_t exec_env, pm_metal_util_tar_iter_t *it)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_tar_iter_next(it);
}

static int32_t pm_metal_util_tar_iter_name_native(wasm_exec_env_t exec_env,
						   const pm_metal_util_tar_iter_t *it, char *out,
						   uint32_t cap)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_tar_iter_name(it, out, (size_t)cap);
}

static uint64_t pm_metal_util_tar_iter_size_native(wasm_exec_env_t exec_env,
						    const pm_metal_util_tar_iter_t *it)
{
	(void)exec_env;
	return pm_metal_util_tar_iter_size(it);
}

static int32_t pm_metal_util_tar_iter_is_dir_native(wasm_exec_env_t exec_env,
						     const pm_metal_util_tar_iter_t *it)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_tar_iter_is_dir(it);
}

static int32_t pm_metal_util_tar_iter_read_native(wasm_exec_env_t exec_env, pm_metal_util_tar_iter_t *it,
						   void *dst, uint32_t dst_cap)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_tar_iter_read(it, dst, (size_t)dst_cap);
}

static void pm_metal_util_tar_iter_close_native(wasm_exec_env_t exec_env, pm_metal_util_tar_iter_t *it)
{
	(void)exec_env;
	pm_metal_util_tar_iter_close(it);
}

static void pm_metal_util_tar_writer_init_native(wasm_exec_env_t exec_env,
						  pm_metal_util_tar_writer_t *w, void *buf, uint32_t cap)
{
	(void)exec_env;
	pm_metal_util_tar_writer_init(w, buf, (size_t)cap);
}

static int32_t pm_metal_util_tar_writer_put_header_native(wasm_exec_env_t exec_env,
							    pm_metal_util_tar_writer_t *w, const char *name,
							    uint64_t size, int32_t is_dir)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_tar_writer_put_header(w, name, size, (int)is_dir);
}

static int32_t pm_metal_util_tar_writer_put_data_native(wasm_exec_env_t exec_env,
							  pm_metal_util_tar_writer_t *w, const void *src,
							  uint32_t src_len)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_tar_writer_put_data(w, src, (size_t)src_len);
}

static int32_t pm_metal_util_tar_writer_put_native(wasm_exec_env_t exec_env,
						     pm_metal_util_tar_writer_t *w, const char *name,
						     int32_t is_dir, const void *src, uint32_t src_len)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_tar_writer_put(w, name, (int)is_dir, src, (size_t)src_len);
}

static int64_t pm_metal_util_tar_writer_finish_native(wasm_exec_env_t exec_env,
						       pm_metal_util_tar_writer_t *w)
{
	(void)exec_env;
	return pm_metal_util_tar_writer_finish(w);
}

static NativeSymbol g_pm_metal_util_tar_native_symbols[] = {
	{"pm_metal_util_tar_iter_init", (void *)pm_metal_util_tar_iter_init_native, "(**~)", NULL},
	{"pm_metal_util_tar_iter_next", (void *)pm_metal_util_tar_iter_next_native, "(*)i", NULL},
	{"pm_metal_util_tar_iter_name", (void *)pm_metal_util_tar_iter_name_native, "(**~)i", NULL},
	{"pm_metal_util_tar_iter_size", (void *)pm_metal_util_tar_iter_size_native, "(*)I", NULL},
	{"pm_metal_util_tar_iter_is_dir", (void *)pm_metal_util_tar_iter_is_dir_native, "(*)i", NULL},
	{"pm_metal_util_tar_iter_read", (void *)pm_metal_util_tar_iter_read_native, "(**~)i", NULL},
	{"pm_metal_util_tar_iter_close", (void *)pm_metal_util_tar_iter_close_native, "(*)", NULL},
	{"pm_metal_util_tar_writer_init", (void *)pm_metal_util_tar_writer_init_native, "(**~)", NULL},
	{"pm_metal_util_tar_writer_put_header", (void *)pm_metal_util_tar_writer_put_header_native, "(*$Ii)i",
	 NULL},
	{"pm_metal_util_tar_writer_put_data", (void *)pm_metal_util_tar_writer_put_data_native, "(**~)i",
	 NULL},
	{"pm_metal_util_tar_writer_put", (void *)pm_metal_util_tar_writer_put_native, "(*$i*~)i", NULL},
	{"pm_metal_util_tar_writer_finish", (void *)pm_metal_util_tar_writer_finish_native, "(*)I", NULL},
};

int pm_metal_util_tar_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_UTIL_TAR_WASI_MODULE, g_pm_metal_util_tar_native_symbols,
					    sizeof(g_pm_metal_util_tar_native_symbols)
						    / sizeof(g_pm_metal_util_tar_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
