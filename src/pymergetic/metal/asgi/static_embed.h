#ifndef PM_METAL_ASGI_STATIC_EMBED_H_
#define PM_METAL_ASGI_STATIC_EMBED_H_

#include <stdint.h>

typedef struct {
    const char *path; /* relative under mount, e.g. index.html */
    const char *ctype;
    const uint8_t *data;
    uint32_t len;
} pm_metal_asgi_static_file_t;

typedef struct {
    const char *url;   /* URL prefix, e.g. /inspect */
    const char *theme; /* default theme from httpd.json */
} pm_metal_asgi_mount_t;

/* From embedded httpd.json static[] (product LIVE has no VFS). */
extern const pm_metal_asgi_mount_t pm_metal_asgi_mounts[];
extern const unsigned pm_metal_asgi_mount_count;

/* rel_path: "" / "/" / "index.html" / "css/base.css" (no mount prefix). */
const pm_metal_asgi_static_file_t *pm_metal_asgi_static_lookup(const char *rel_path);

#endif
