/*
 * Iface packages + symbol table (docs/DOC_IFACE_PLAN.md Part II) — a
 * small registry of build-time header packs (default lz4(ustar), see
 * PM_METAL_IFACE_PKG_HEADERS) plus a plain C array of every registered
 * NativeSymbol, scraped at build time (scripts/gen_iface_syms.py) — not
 * a second hand-written signature list. This is a *reflection* surface
 * (browse what a package or an import table contains); it is not the
 * doc catalog (util/doc.h owns summary/sig/body text) — a sym row's
 * optional `doc_key` is only a pointer into that catalog.
 *
 * A registered pack's raw bytes decompress once, at pm_metal_iface_pkg_register()
 * time, into a fixed per-package host buffer (see iface.c's
 * PM_METAL_IFACE_PKG_MAX / uncompressed-size cap) — every file_at()/
 * file_open() call after that walks the already-inflated ustar via
 * util/tar.h, same buffer-owned-by-the-registry pattern util/tar.h's
 * iter_t itself documents.
 *
 * Host-only storage (like util/doc.h) — a wasm guest reaches it through
 * the buffer-out imports below (same reasoning as util/tar.h's
 * iter_name(): a host pointer is never a valid wasm app address).
 *
 * impl: common — src/pymergetic/metal/util/iface.c
 * impl: wasi import — src/pymergetic/metal/util/iface.c (wasm32 only)
 */
#ifndef PYMERGETIC_METAL_UTIL_IFACE_H_
#define PYMERGETIC_METAL_UTIL_IFACE_H_

#include <stddef.h>
#include <stdint.h>

#include "pymergetic/metal/wasi.h" /* IWYU pragma: keep */

/* This module's own import_module name — see iface.c's native_register()
 * for the host side that must build from this exact same constant. */
#define PM_METAL_UTIL_IFACE_WASI_MODULE "pymergetic.metal.util.iface"

#if defined(__wasm__)
#define PM_METAL_UTIL_IFACE_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_UTIL_IFACE_WASI_MODULE, name)
#endif

/* ustar name field width (see util/tar.h's own PM_METAL_UTIL_TAR_NAME_MAX). */
#define PM_METAL_IFACE_PATH_MAX 160U

typedef enum {
  PM_METAL_IFACE_PKG_HEADERS = 1, /* lz4(ustar) of public .h files */
  PM_METAL_IFACE_PKG_SYSROOT = 2, /* reserved — never inside metal.guest, see file header */
  PM_METAL_IFACE_PKG_SOURCES = 3, /* lz4(ustar) of curated matching .c (Kconfig) */
  PM_METAL_IFACE_PKG_META    = 4  /* lz4(ustar) of project prose (LICENSE, README, markdown under docs/) */
} pm_metal_iface_pkg_kind_t;

/** "headers" / "sysroot" / "sources" / "meta" / "unknown" — static literal. */
static inline const char *pm_metal_iface_pkg_kind_str(pm_metal_iface_pkg_kind_t kind)
{
  if (kind == PM_METAL_IFACE_PKG_HEADERS) {
    return "headers";
  }
  if (kind == PM_METAL_IFACE_PKG_SYSROOT) {
    return "sysroot";
  }
  if (kind == PM_METAL_IFACE_PKG_SOURCES) {
    return "sources";
  }
  if (kind == PM_METAL_IFACE_PKG_META) {
    return "meta";
  }
  return "unknown";
}

#if !defined(__wasm__)

/* Read-only view of one registered package — pointers borrowed from
 * the registry's own storage, valid for the package's whole lifetime
 * (packages are never unregistered in v1). */
typedef struct {
  const char                *name;
  pm_metal_iface_pkg_kind_t  kind;
  const char                *version;  /* never NULL; "" if unset */
  const char                *abi_hash; /* never NULL; "" if unset */
  uint32_t                   nfiles;
  uint32_t                   blob_len; /* compressed size, as registered */
} pm_metal_iface_pkg_info_t;

/* One NativeSymbol row (scripts/gen_iface_syms.py's own harvest — see
 * that script + generated iface_syms.inc.c, never hand-written). */
typedef struct {
  const char *module;
  const char *name;
  const char *sig;
  uint8_t     class_;
  const char *doc_key; /* "" if this native has no doc catalog entry */
} pm_metal_iface_sym_t;

int32_t pm_metal_iface_pkg_count(void);
int32_t pm_metal_iface_pkg_at(uint32_t i, pm_metal_iface_pkg_info_t *out);
int32_t pm_metal_iface_pkg_info(const char *name, pm_metal_iface_pkg_info_t *out);

/*
 * Registers one package — decompresses blob (lz4 of a ustar archive,
 * blob_len compressed bytes, uncompressed_len its ustar size) into this
 * package's own host buffer, then counts ustar entries for nfiles.
 * uncompressed_len == blob_len is allowed (raw ustar, debug opt-out —
 * see file header) and skips the lz4 step. 0 ok, -1 if the registry is
 * full (PM_METAL_IFACE_PKG_MAX, iface.c), the name is already taken, or
 * decompression/ustar-walk failed.
 */
int32_t pm_metal_iface_pkg_register(const char                *name,
                                    pm_metal_iface_pkg_kind_t  kind,
                                    const char                *version,
                                    const char                *abi_hash,
                                    const uint8_t              *blob,
                                    uint32_t                    blob_len,
                                    uint32_t                    uncompressed_len);

int32_t pm_metal_iface_file_count(const char *pkg);
/* NUL-terminated path into out/cap — same convention as util/tar.h's
 * iter_name(). -1 if pkg is unknown or i is out of range. */
int32_t pm_metal_iface_file_at(const char *pkg, uint32_t i, char *out, uint32_t cap);
/* data/len (out-params) borrow directly into the package's own inflated
 * ustar buffer — valid for the package's whole lifetime, never freed by
 * the caller. -1 if pkg or path is unknown. */
int32_t pm_metal_iface_file_open(const char *pkg, const char *path, const uint8_t **data, uint32_t *len);

int32_t pm_metal_iface_sym_count(void);
int32_t pm_metal_iface_sym_at(uint32_t i, pm_metal_iface_sym_t *out);
int32_t pm_metal_iface_sym_lookup(const char *module, const char *name, pm_metal_iface_sym_t *out);

/*
 * Registers this module's own wasi-style imports (see
 * PM_METAL_UTIL_IFACE_WASI_MODULE above) — host-only, never included by
 * a mod. Call once, after wasm_runtime_full_init() has succeeded and
 * before the first load()/instantiate() of any module that might import
 * these (guest/wasm/wasm.c's pm_metal_wasm_init() is the caller).
 * Returns 0 on success, -1 if WAMR rejected the registration.
 */
int pm_metal_util_iface_native_register(void);

/*
 * Registers the "metal.guest" (+ "mod.t8_multimod_lib") header packs
 * (scripts/build.d/port/efi/embed-iface.sh generated
 * util/iface_metal_guest_embed.inc.c) — call once at boot, before any
 * shell/py access to `iface`/`pymergetic.metal.iface` (guest/wasm/wasm.c's
 * pm_metal_wasm_init() is the caller, mirrors py_zip_embed.c's own
 * pm_metal_py_zip_embed_install()).
 */
void pm_metal_iface_embed_install(void);

/*
 * Registers optional ESP sidecar packs: for each mods/apps/<app>/iface.list,
 * load the named lz4(ustar) blobs and call pm_metal_iface_pkg_register().
 * Soft-fail (log + skip) on missing/malformed rows — never fatal.
 * EFI: call while SimpleFileSystem is still live (MetalPkg/main.c, after
 * mods/apps preload) — post-EBS readdir cannot see app dirs from cache
 * alone. Also invoked from wasm.c (BIOS / idempotent if already done).
 */
void pm_metal_iface_esp_install(void);

#else /* __wasm__ */

/* Buffer-out guest ABI (see file header re: host-only storage). cap 0
 * skips that field entirely, same convention as util/doc.h / util/tar.h. */

extern int32_t pm_metal_iface_pkg_count(void) PM_METAL_UTIL_IFACE_IMPORT(pm_metal_iface_pkg_count);

extern int32_t pm_metal_iface_pkg_at(uint32_t i,
                                     char     *name,
                                     uint32_t  name_cap,
                                     uint32_t *out_kind,
                                     char     *version,
                                     uint32_t  version_cap,
                                     char     *abi_hash,
                                     uint32_t  abi_hash_cap,
                                     uint32_t *out_nfiles,
                                     uint32_t *out_blob_len)
  PM_METAL_UTIL_IFACE_IMPORT(pm_metal_iface_pkg_at);

extern int32_t pm_metal_iface_file_count(const char *pkg)
  PM_METAL_UTIL_IFACE_IMPORT(pm_metal_iface_file_count);

extern int32_t pm_metal_iface_file_at(const char *pkg, uint32_t i, char *out, uint32_t cap)
  PM_METAL_UTIL_IFACE_IMPORT(pm_metal_iface_file_at);

/* Copies (does not borrow — a host buffer pointer is never valid guest
 * memory) up to dst_cap bytes of the file's data into dst. Returns the
 * file's own total length (may exceed dst_cap — read again with a
 * bigger buffer, no partial-read cursor here, files in v1 packs are
 * small), or -1 if pkg/path is unknown. */
extern int32_t pm_metal_iface_file_read(const char *pkg,
                                        const char *path,
                                        uint8_t    *dst,
                                        uint32_t    dst_cap)
  PM_METAL_UTIL_IFACE_IMPORT(pm_metal_iface_file_read);

extern int32_t pm_metal_iface_sym_count(void) PM_METAL_UTIL_IFACE_IMPORT(pm_metal_iface_sym_count);

extern int32_t pm_metal_iface_sym_at(uint32_t i,
                                     char     *module,
                                     uint32_t  module_cap,
                                     char     *name,
                                     uint32_t  name_cap,
                                     char     *sig,
                                     uint32_t  sig_cap,
                                     uint32_t *out_class,
                                     char     *doc_key,
                                     uint32_t  doc_key_cap) PM_METAL_UTIL_IFACE_IMPORT(pm_metal_iface_sym_at);

extern int32_t pm_metal_iface_sym_lookup(const char *module,
                                         const char *name,
                                         char       *sig,
                                         uint32_t    sig_cap,
                                         uint32_t   *out_class,
                                         char       *doc_key,
                                         uint32_t    doc_key_cap)
  PM_METAL_UTIL_IFACE_IMPORT(pm_metal_iface_sym_lookup);

#endif /* __wasm__ */

#endif /* PYMERGETIC_METAL_UTIL_IFACE_H_ */
