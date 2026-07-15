/*
 * pm_metal_util_tar_* — impl: common (see util/tar.h; wasm32 mods reach
 * this same code via this file's own wasi-style import registration at
 * the bottom, not via a second compiled copy of this file, and never
 * link a byte of upstream microtar themselves).
 *
 * Layout: pm_metal_util_tar_iter_t/pm_metal_util_tar_writer_t are both
 * reinterpreted in place as pm_metal_util_tar_state_t below — same
 * "_Static_assert the size fits" shape as port/lock.c. One state struct
 * for both directions (a vendored mtar_t + this file's own memory-stream
 * context) — a writer simply never populates the read-only fields
 * (hdr/data_read), trading a few unused bytes for one shared set of
 * memory-stream callbacks instead of two.
 */
#include "pymergetic/metal/util/tar.h"

#include <limits.h>
#include <string.h>

/* vendored — full path from this package's own root (see CMakeLists.txt's
 * PM_METAL_ROOT include dir, scoped to just this file), not a bare
 * "microtar.h"; see util/lz4.c for the same convention. */
#include "external/microtar/src/microtar.h"

typedef struct {
	void *base;   /* archive bytes — read mode: caller's `buf`; write mode: caller's output `buf` */
	size_t limit; /* read mode: `len` given to iter_init(); write mode: `cap` given to writer_init() */
} pm_metal_util_tar_mem_t;

typedef struct {
	mtar_t tar;
	pm_metal_util_tar_mem_t mem;
	mtar_header_t hdr;   /* iter only: cached header of the current entry */
	size_t data_read;    /* iter only: bytes of hdr already handed out via iter_read() */
	int have_entry;      /* iter only: 1 once next() has produced a current entry */
} pm_metal_util_tar_state_t;

_Static_assert(sizeof(pm_metal_util_tar_state_t) <= sizeof(pm_metal_util_tar_iter_t),
	       "pm_metal_util_tar_state_t does not fit in pm_metal_util_tar_iter_t storage");
_Static_assert(sizeof(pm_metal_util_tar_state_t) <= sizeof(pm_metal_util_tar_writer_t),
	       "pm_metal_util_tar_state_t does not fit in pm_metal_util_tar_writer_t storage");

/*
 * Memory-stream callbacks — bounds-checked read/write directly against
 * the caller-owned buffer handed to iter_init()/writer_init(), no libc
 * FILE* involved. tar->pos is always the *pre*-operation offset here:
 * microtar's own tread()/twrite() wrappers advance it themselves right
 * after calling us. Promoting to size_t before adding avoids a 32-bit
 * wraparound false-negative on the bounds check near UINT_MAX.
 */
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
	return MTAR_ESUCCESS; /* nothing owned beyond the caller's own buffer */
}

void pm_metal_util_tar_iter_init(pm_metal_util_tar_iter_t *it, const void *buf, size_t len)
{
	pm_metal_util_tar_state_t *st = (pm_metal_util_tar_state_t *)it;

	memset(st, 0, sizeof(*st));
	/* Cast away const: this read-mode mem context is only ever touched by
	 * mem_read()/mem_seek() above, which never write through it. */
	st->mem.base = (void *)buf;
	st->mem.limit = len;
	st->tar.read = pm_metal_util_tar_mem_read;
	st->tar.seek = pm_metal_util_tar_mem_seek;
	st->tar.close = pm_metal_util_tar_mem_close;
	st->tar.stream = &st->mem;
}

int pm_metal_util_tar_iter_next(pm_metal_util_tar_iter_t *it)
{
	pm_metal_util_tar_state_t *st = (pm_metal_util_tar_state_t *)it;
	int err;

	if (st->have_entry) {
		/* A caller may not have fully drained the current entry via
		 * iter_read() — re-anchor on its own header before asking
		 * microtar to skip it, since mtar_next()/mtar_read_data()
		 * both assume tar->pos already sits there. */
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

	/* Force the first iter_read() call for this entry to (re-)seek past
	 * its header and load remaining_data — see mtar_read_data()'s own
	 * "if (tar->remaining_data == 0)" branch. */
	st->tar.remaining_data = 0;
	st->data_read = 0;
	st->have_entry = 1;
	return 1;
}

int pm_metal_util_tar_iter_name(const pm_metal_util_tar_iter_t *it, char *out, size_t cap)
{
	const pm_metal_util_tar_state_t *st = (const pm_metal_util_tar_state_t *)it;
	size_t n;

	if (!st->have_entry || !out || cap == 0) {
		return -1;
	}

	n = strlen(st->hdr.name);
	if (n + 1 > cap) {
		return -1;
	}
	memcpy(out, st->hdr.name, n + 1);
	return (int)n;
}

uint64_t pm_metal_util_tar_iter_size(const pm_metal_util_tar_iter_t *it)
{
	const pm_metal_util_tar_state_t *st = (const pm_metal_util_tar_state_t *)it;

	return st->have_entry ? (uint64_t)st->hdr.size : 0;
}

int pm_metal_util_tar_iter_is_dir(const pm_metal_util_tar_iter_t *it)
{
	const pm_metal_util_tar_state_t *st = (const pm_metal_util_tar_state_t *)it;

	if (!st->have_entry) {
		return -1;
	}
	return st->hdr.type == MTAR_TDIR ? 1 : 0;
}

int pm_metal_util_tar_iter_read(pm_metal_util_tar_iter_t *it, void *dst, size_t dst_cap)
{
	pm_metal_util_tar_state_t *st = (pm_metal_util_tar_state_t *)it;
	size_t remaining, want;

	if (!st->have_entry || !dst) {
		return -1;
	}
	if (st->data_read >= (size_t)st->hdr.size) {
		return 0; /* this entry's data is fully consumed */
	}

	remaining = (size_t)st->hdr.size - st->data_read;
	want = dst_cap < remaining ? dst_cap : remaining;
	if (want > (size_t)INT_MAX) {
		want = (size_t)INT_MAX; /* keep the returned byte count representable */
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

void pm_metal_util_tar_writer_init(pm_metal_util_tar_writer_t *w, void *buf, size_t cap)
{
	pm_metal_util_tar_state_t *st = (pm_metal_util_tar_state_t *)w;

	memset(st, 0, sizeof(*st));
	st->mem.base = buf;
	st->mem.limit = cap;
	st->tar.write = pm_metal_util_tar_mem_write;
	st->tar.seek = pm_metal_util_tar_mem_seek;
	st->tar.close = pm_metal_util_tar_mem_close;
	st->tar.stream = &st->mem;
}

int pm_metal_util_tar_writer_put_header(pm_metal_util_tar_writer_t *w, const char *name, uint64_t size,
					 int is_dir)
{
	pm_metal_util_tar_state_t *st = (pm_metal_util_tar_state_t *)w;
	int err;

	if (!name || strlen(name) >= PM_METAL_UTIL_TAR_NAME_MAX) {
		return -1;
	}
	if (is_dir && size != 0) {
		return -1;
	}
	if (size > (uint64_t)UINT_MAX) {
		return -1; /* upstream mtar_header_t.size is a plain 32-bit unsigned */
	}
	if (st->tar.remaining_data != 0) {
		return -1; /* previous entry's put_data() calls didn't add up to its own size yet */
	}

	err = is_dir ? mtar_write_dir_header(&st->tar, name)
		     : mtar_write_file_header(&st->tar, name, (unsigned)size);

	return err == MTAR_ESUCCESS ? 0 : -1;
}

int pm_metal_util_tar_writer_put_data(pm_metal_util_tar_writer_t *w, const void *src, size_t src_len)
{
	pm_metal_util_tar_state_t *st = (pm_metal_util_tar_state_t *)w;

	if (src_len == 0) {
		return 0;
	}
	if (!src || src_len > (size_t)st->tar.remaining_data || src_len > (size_t)UINT_MAX) {
		return -1; /* would overrun the entry's own put_header() size */
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
	pm_metal_util_tar_state_t *st = (pm_metal_util_tar_state_t *)w;

	if (st->tar.remaining_data != 0) {
		return -1; /* last entry's put_data() calls didn't add up to its own size yet */
	}
	if (mtar_finalize(&st->tar) != MTAR_ESUCCESS) {
		return -1;
	}
	return (int64_t)st->tar.pos;
}

/*
 * wasi-style import bridge — see size.c's own bridge comment for the
 * general signature-string rules this follows. it/w cross as a bare '*'
 * (checked length 1, not the full opaque struct size — same accepted
 * trade-off as arena.h's opaque handles, see arena.c); name crosses as
 * '$' (NUL-terminated string, same as log.c's msg); every real data
 * buffer (buf/out/dst/src) is a '*'+'~' pair, never a bare '*', since
 * unlike it/w a guest also reads/writes through these directly.
 */
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
