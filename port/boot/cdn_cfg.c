#include "pymergetic/metal/cdn.h"

#include <string.h>

#if defined(MICROPY_PY_WASM) && MICROPY_PY_WASM
#include "extmod/wasmmod/cdn.h"
#endif

/*
 * Master home CDN (official realm). Lab / own-realm: -DMETAL_CDN_URL=...
 * e.g. http://127.0.0.1:8000/cdn or https://cdn.example/cdn
 */
#ifndef METAL_CDN_URL
#define METAL_CDN_URL "https://cdn.pymergetic.com/cdn"
#endif

/* Optional bake extras (own mirrors): space- and/or comma-separated bases. */
#ifndef METAL_CDN_EXTRA_URLS
#define METAL_CDN_EXTRA_URLS ""
#endif

#ifndef METAL_CDN_URL_MAX
#define METAL_CDN_URL_MAX 192
#endif

static char g_site[METAL_CDN_URL_MAX];
static pm_metal_cdn_mode_t g_site_mode = PM_METAL_CDN_MODE_ADD;

static void trim_trailing_slash(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == '/') {
        s[--n] = '\0';
    }
}

const char *pm_metal_cdn_default_url(void)
{
    const char *u = METAL_CDN_URL;
    if (u == NULL || u[0] == '\0') {
        return NULL;
    }
    return u;
}

const char *pm_metal_cdn_site_url(void)
{
    return g_site[0] != '\0' ? g_site : NULL;
}

pm_metal_cdn_mode_t pm_metal_cdn_site_mode(void)
{
    return g_site_mode;
}

void pm_metal_cdn_set_site(const char *url, pm_metal_cdn_mode_t mode)
{
    g_site[0] = '\0';
    g_site_mode = mode;
    if (mode == PM_METAL_CDN_MODE_OFF) {
        return;
    }
    if (url == NULL || url[0] == '\0') {
        return;
    }
    strncpy(g_site, url, METAL_CDN_URL_MAX - 1);
    g_site[METAL_CDN_URL_MAX - 1] = '\0';
    trim_trailing_slash(g_site);
}

void pm_metal_cdn_set_site_spec(const char *spec)
{
    if (spec == NULL || spec[0] == '\0') {
        return;
    }
    if (strcmp(spec, "off") == 0 || strcmp(spec, "OFF") == 0) {
        pm_metal_cdn_set_site(NULL, PM_METAL_CDN_MODE_OFF);
        return;
    }
    if (strncmp(spec, "replace:", 8) == 0) {
        pm_metal_cdn_set_site(spec + 8, PM_METAL_CDN_MODE_REPLACE);
        return;
    }
    if (strncmp(spec, "add:", 4) == 0) {
        pm_metal_cdn_set_site(spec + 4, PM_METAL_CDN_MODE_ADD);
        return;
    }
    pm_metal_cdn_set_site(spec, PM_METAL_CDN_MODE_ADD);
}

#if defined(MICROPY_PY_WASM) && MICROPY_PY_WASM
static void bind_extras(void)
{
    const char *raw = METAL_CDN_EXTRA_URLS;
    char buf[METAL_CDN_URL_MAX];
    size_t i = 0;

    if (raw == NULL || raw[0] == '\0') {
        return;
    }
    while (*raw) {
        while (*raw == ' ' || *raw == '\t' || *raw == ',') {
            raw++;
        }
        if (*raw == '\0') {
            break;
        }
        i = 0;
        while (*raw && *raw != ' ' && *raw != '\t' && *raw != ',' && i + 1 < sizeof(buf)) {
            buf[i++] = *raw++;
        }
        buf[i] = '\0';
        if (i == 0) {
            continue;
        }
        trim_trailing_slash(buf);
        (void)mp_wasm_cdn_add(buf, NULL);
    }
}
#endif

int pm_metal_cdn_bind(void)
{
#if defined(MICROPY_PY_WASM) && MICROPY_PY_WASM
    const char *home = pm_metal_cdn_default_url();
    const char *site = pm_metal_cdn_site_url();
    pm_metal_cdn_mode_t mode = pm_metal_cdn_site_mode();

    if (mode == PM_METAL_CDN_MODE_OFF) {
        mp_wasm_cdn_reset();
        return 0;
    }
    if (mode == PM_METAL_CDN_MODE_REPLACE) {
        if (site == NULL) {
            mp_wasm_cdn_reset();
            return 0;
        }
        mp_wasm_cdn_configure(site, NULL);
        return 0;
    }
    /* ADD: home → bake extras → site prepended (tried first). */
    if (home != NULL) {
        mp_wasm_cdn_configure(home, NULL);
    } else {
        mp_wasm_cdn_reset();
    }
    bind_extras();
    if (site != NULL) {
        (void)mp_wasm_cdn_prepend(site, NULL);
    }
    return 0;
#else
    return 0;
#endif
}
