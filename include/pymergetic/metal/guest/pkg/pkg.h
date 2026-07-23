/*
 * Named guest package registry.
 *
 * Guest binary layout is FIXED by the framework (not listed per package):
 *   mods/apps/<name>/<name>.<host_aot_arch>.aot   (+ optional .sig)
 *   mods/apps/<name>/<name>.wasm                  (+ optional .sig)
 * Host picks arch via pm_metal_host_aot_arch(). HTTP/ESP only ever asks for
 * *this* host's AOT (or wasm) — never a baked multi-arch table.
 *
 * Packages register only their extra assets (e.g. doom1.wad) + optional ready().
 * Host-only.
 */
#ifndef PYMERGETIC_METAL_GUEST_PKG_PKG_H_
#define PYMERGETIC_METAL_GUEST_PKG_PKG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/** One seed/fetch slot (paths are absolute under ESP / HTTP root). */
typedef struct {
	const char *esp_path;
	const char *url_path;
	uint32_t    cap;
} pm_metal_pkg_file_t;

/** Extra asset under mods/apps/<pkg>/ (not the guest .aot/.wasm). */
typedef struct {
	const char *name; /* e.g. "doom1.wad" */
	uint32_t    cap;
} pm_metal_pkg_asset_t;

typedef struct pm_metal_pkg {
	const char                 *name;
	const pm_metal_pkg_asset_t *assets;
	uint32_t                    nassets;
	/**
	 * 1 if playable from ESP. NULL = guest binary present
	 * (host .aot or .wasm). Custom ready must include guest + assets.
	 */
	int (*ready)(void);
} pm_metal_pkg_t;

/** wamrc / filename infix for this kernel build (i386, x86_64, …). */
const char *pm_metal_host_aot_arch(void);

/** 1 if mods/apps/<name>/<name>.<arch>.aot or .wasm is on ESP. */
int pm_metal_pkg_guest_ready(const char *name);

void pm_metal_pkg_init(void);

int pm_metal_pkg_register(const pm_metal_pkg_t *pkg);

const pm_metal_pkg_t *pm_metal_pkg_lookup(const char *name);

int pm_metal_pkg_ready(const char *name);

/**
 * Build dynamic seed plan for <name>: host AOT (+sig), wasm (+sig), assets.
 * Pointers valid until the next call. out_n set to count (0 if unknown).
 */
const pm_metal_pkg_file_t *pm_metal_pkg_files(const char *name, uint32_t *out_n);

/** 1 if seed slot may be skipped (wrong/optional / already satisfied). */
int pm_metal_pkg_file_optional(const char *name, const pm_metal_pkg_file_t *f);

int pm_metal_pkg_ensure(const char *name);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_GUEST_PKG_PKG_H_ */
