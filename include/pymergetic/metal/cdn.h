/*
 * Platform CDN policy (all seats: BIOS / UEFI / browser).
 *
 * Master / product builds bake the official realm as home:
 *   https://cdn.pymergetic.com/cdn
 * Lab / own-realm: override at bake (METAL_CDN_URL) or at site
 * (DHCP opt 224 / iPXE / browser session). Never confuse "where I publish
 * packs while developing" with "what the kernel's home shelf is".
 *
 * Layers:
 *   1) Home — baked METAL_CDN_URL (master default = official metal-cdn)
 *   2) Bake extras — METAL_CDN_EXTRA_URLS (space/comma-separated; appended)
 *   3) Site — DHCP / iPXE / session: add (prepend, default), replace:URL, or off
 *
 * Applied in pm_metal_autoexec() after ready + after mp_init.
 * Browser session must ADD the page CDN, never wipe home.
 */
#ifndef PYMERGETIC_METAL_CDN_H_
#define PYMERGETIC_METAL_CDN_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pm_metal_cdn_mode {
    PM_METAL_CDN_MODE_ADD = 0,     /* prepend site; keep home (+ extras) */
    PM_METAL_CDN_MODE_REPLACE = 1, /* only site URL */
    PM_METAL_CDN_MODE_OFF = 2,     /* no remote CDN */
} pm_metal_cdn_mode_t;

/* Parse "http…", "add:http…", "replace:http…", or "off". */
void pm_metal_cdn_set_site_spec(const char *spec);

/* Optional explicit setters (tests / browser). */
void pm_metal_cdn_set_site(const char *url, pm_metal_cdn_mode_t mode);

const char *pm_metal_cdn_default_url(void);
const char *pm_metal_cdn_site_url(void);
pm_metal_cdn_mode_t pm_metal_cdn_site_mode(void);

/**
 * Apply policy to wasmmod (when MICROPY_PY_WASM).
 * Returns 0 if bound or intentionally off; -1 if wasmmod missing / fail.
 */
int pm_metal_cdn_bind(void);

#ifdef __cplusplus
}
#endif

#endif
