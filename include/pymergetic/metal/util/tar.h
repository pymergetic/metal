/*
 * Minimal ustar reader/writer — thin wrapper around vendored upstream
 * microtar (external/microtar, pinned + patched by
 * scripts/setup microtar, see docs/SOURCETREE.md "Vendoring"):
 * microtar owns header (de)serialization + checksum handling, this
 * module adds bounds-checked memory-stream callbacks over a caller-owned
 * buffer (no FILE*, no filesystem) and a small sequential cursor/writer
 * API on top.
 *
 * Guest-facing iter_t/writer_t hold only an opaque host id; full tar
 * state (including microtar function pointers) lives in a host-only
 * slot table. The only guest memory touched for archive bytes is the
 * caller-owned buffer. To read/write a *compressed*
 * archive, compose with util/lz4.h at the call site (lz4_decompress()
 * into your own buffer, then iter_init() that buffer; or writer_finish()
 * into a buffer, then lz4_compress() it) — this header has no idea lz4
 * exists, same independence as size.h/arena.h from each other.
 *
 * Deliberately ustar-only, no GNU/PAX long-name extension: names >99
 * bytes are rejected outright (see PM_METAL_UTIL_TAR_NAME_MAX) rather
 * than silently truncated or spilled into a second header record — less
 * code, and every entry stays exactly one fixed 512-byte header, no
 * variable-length lookahead when reading.
 *
 * Single implementation, host-side only (src/pymergetic/metal/
 * util/tar.c; see util/size.h for the general pattern this follows) — a
 * mod never links a byte of upstream microtar itself, only ever calls
 * through this module's own wasi-style import bridge, same as
 * lz4.h/size.h/arena.h/log.h. Entry name/data never cross the wasm
 * boundary as bare pointers (a host pointer is not a valid wasm app
 * address — see log.h's level_name() for the same reasoning): reading is
 * buffer-out (iter_name()) + getters (iter_size()/iter_is_dir()) +
 * chunked copy (iter_read()), writing takes the name as a NUL-terminated
 * '$' string and data as ordinary '*~' buffers, exactly like every other
 * util/ import here.
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#ifndef PYMERGETIC_METAL_UTIL_TAR_H_
#define PYMERGETIC_METAL_UTIL_TAR_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

/* This module's own import_module name — see tar.c's native_register()
 * for the host side that must build from this exact same constant. */
#define PM_METAL_UTIL_TAR_WASI_MODULE "pymergetic.metal.util.tar"

#if defined(__wasm__)
#define PM_METAL_UTIL_TAR_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_UTIL_TAR_WASI_MODULE, name)
#endif

/* ustar's own fixed name-field width (incl. NUL) — see file header re:
 * no GNU/PAX long-name support. */
#define PM_METAL_UTIL_TAR_NAME_MAX 100U

/*
 * Opaque cursor — only a host handle id. Zero-init before first init();
 * call iter_close() when done (or before reusing). Never interpret `id`.
 */
typedef struct pm_metal_util_tar_iter {
	uint32_t id;
} pm_metal_util_tar_iter_t;

/*
 * Starts walking len bytes of ustar-formatted data at buf — buf is owned
 * by the caller for the iterator's lifetime, init() never copies it.
 * Does not read or validate anything yet; the first header is only
 * touched by the first iter_next() call.
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern void pm_metal_util_tar_iter_init(pm_metal_util_tar_iter_t *it, const void *buf, size_t len)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_iter_init);
#else
void pm_metal_util_tar_iter_init(pm_metal_util_tar_iter_t *it, const void *buf, size_t len);
#endif

/*
 * Advances to the next entry. Returns 1 (an entry is now current — query
 * it with iter_name()/iter_size()/iter_is_dir() below, read its bytes
 * with iter_read()), 0 at end of archive, or -1 on a malformed header
 * (bad checksum/magic) or one that runs past the buffer given to
 * init().
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_tar_iter_next(pm_metal_util_tar_iter_t *it)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_iter_next);
#else
int pm_metal_util_tar_iter_next(pm_metal_util_tar_iter_t *it);
#endif

/*
 * Current entry's name, NUL-terminated, into out/cap — same buf+cap+
 * snprintf-style-length convention as util/size.h's format(). Returns
 * the name's length, or -1 if there is no current entry or cap is too
 * small.
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_tar_iter_name(const pm_metal_util_tar_iter_t *it, char *out, size_t cap)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_iter_name);
#else
int pm_metal_util_tar_iter_name(const pm_metal_util_tar_iter_t *it, char *out, size_t cap);
#endif

/*
 * Current entry's byte size (0 for directories, or if there is no
 * current entry).
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern uint64_t pm_metal_util_tar_iter_size(const pm_metal_util_tar_iter_t *it)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_iter_size);
#else
uint64_t pm_metal_util_tar_iter_size(const pm_metal_util_tar_iter_t *it);
#endif

/*
 * 1 if the current entry is a directory, 0 if a regular file, -1 if
 * there is no current entry.
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_tar_iter_is_dir(const pm_metal_util_tar_iter_t *it)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_iter_is_dir);
#else
int pm_metal_util_tar_iter_is_dir(const pm_metal_util_tar_iter_t *it);
#endif

/*
 * Copies up to dst_cap bytes of the *current* entry's file data into
 * dst, continuing from wherever the previous iter_read() call for this
 * entry left off — call repeatedly until iter_size() bytes total have
 * been read; never reads past that entry's own boundary even if dst_cap
 * is bigger. Lets a large file be read in fixed-size chunks without ever
 * holding it whole in RAM. Returns bytes copied (0 once the entry is
 * exhausted), or -1 on a truncated/malformed entry or no current entry.
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_tar_iter_read(pm_metal_util_tar_iter_t *it, void *dst, size_t dst_cap)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_iter_read);
#else
int pm_metal_util_tar_iter_read(pm_metal_util_tar_iter_t *it, void *dst, size_t dst_cap);
#endif

/*
 * Release the host slot for this iterator. Safe on a zeroed or already
 * closed handle.
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern void pm_metal_util_tar_iter_close(pm_metal_util_tar_iter_t *it)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_iter_close);
#else
void pm_metal_util_tar_iter_close(pm_metal_util_tar_iter_t *it);
#endif

/*
 * Opaque writer — host handle id only (see iter_t). writer_finish()
 * releases the slot.
 */
typedef struct pm_metal_util_tar_writer {
	uint32_t id;
} pm_metal_util_tar_writer_t;

/*
 * Starts building an archive into buf (capacity cap) — buf is owned by
 * the caller for the writer's lifetime, init() never allocates.
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern void pm_metal_util_tar_writer_init(pm_metal_util_tar_writer_t *w, void *buf, size_t cap)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_writer_init);
#else
void pm_metal_util_tar_writer_init(pm_metal_util_tar_writer_t *w, void *buf, size_t cap);
#endif

/*
 * Starts one new entry: writes its 512-byte ustar header and remembers
 * size for the put_data() calls that must follow (skip them entirely for
 * a directory — is_dir implies size 0). name must fit
 * PM_METAL_UTIL_TAR_NAME_MAX incl. NUL. Returns 0, or -1 if the header
 * wouldn't fit in the writer's own capacity, name is too long, or a
 * previous entry's put_data() calls didn't yet add up to its own size.
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_tar_writer_put_header(pm_metal_util_tar_writer_t *w, const char *name,
						uint64_t size, int is_dir)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_writer_put_header);
#else
int pm_metal_util_tar_writer_put_header(pm_metal_util_tar_writer_t *w, const char *name, uint64_t size,
					 int is_dir);
#endif

/*
 * Appends src_len bytes of the *current* entry's file data (the one last
 * started with put_header()) — callable repeatedly to stream a large
 * file in fixed-size chunks without ever holding it whole in RAM; the
 * running total across every put_data() call for one entry must equal
 * exactly that entry's own put_header() size (ustar's own end-of-record
 * padding is written automatically once it does). Returns 0, or -1 on
 * overflow (writer capacity) or overrun (more bytes than that entry's
 * own header promised).
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_tar_writer_put_data(pm_metal_util_tar_writer_t *w, const void *src,
					      size_t src_len)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_writer_put_data);
#else
int pm_metal_util_tar_writer_put_data(pm_metal_util_tar_writer_t *w, const void *src, size_t src_len);
#endif

/*
 * Convenience: put_header() + one put_data() call, for the common case
 * where a whole small file already sits in one buffer. is_dir => src/
 * src_len are ignored (pass NULL/0). Returns 0, or -1 (see put_header()/
 * put_data() above for the cases that can fail).
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern int pm_metal_util_tar_writer_put(pm_metal_util_tar_writer_t *w, const char *name, int is_dir,
					 const void *src, size_t src_len)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_writer_put);
#else
int pm_metal_util_tar_writer_put(pm_metal_util_tar_writer_t *w, const char *name, int is_dir,
				  const void *src, size_t src_len);
#endif

/*
 * Writes the two required all-zero 512-byte end-of-archive blocks — call
 * once, after the last entry (and after that entry's put_data() calls
 * have added up to its own size). Returns the total archive size written
 * (everything since init(), including these two blocks), or -1 if they
 * don't fit in the writer's own capacity or the last entry is still
 * incomplete.
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 * impl: wasi import — src/pymergetic/metal/util/tar.c (wasm32 only)
 */
#if defined(__wasm__)
extern int64_t pm_metal_util_tar_writer_finish(pm_metal_util_tar_writer_t *w)
	PM_METAL_UTIL_TAR_IMPORT(pm_metal_util_tar_writer_finish);
#else
int64_t pm_metal_util_tar_writer_finish(pm_metal_util_tar_writer_t *w);
#endif

#if !defined(__wasm__)
/*
 * Registers this module's own wasi-style imports (see
 * PM_METAL_UTIL_TAR_WASI_MODULE above) — host-only, never included by
 * a mod. Call once, after wasm_runtime_full_init() has succeeded and
 * before the first load()/instantiate() of any module that might import
 * these (runtime.c's init() is the only caller today). Returns 0 on
 * success, -1 if WAMR rejected the registration.
 *
 * impl: common — src/pymergetic/metal/util/tar.c
 */
int pm_metal_util_tar_native_register(void);
#endif

#endif /* PYMERGETIC_METAL_UTIL_TAR_H_ */
