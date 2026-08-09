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
    const char *root;  /* VFS root, e.g. /mods/.../www/inspect */
    const char *theme; /* default theme from httpd.json */
} pm_metal_asgi_mount_t;

/* From embedded httpd.json static[]. */
extern const pm_metal_asgi_mount_t pm_metal_asgi_mounts[];
extern const unsigned pm_metal_asgi_mount_count;

/* ROM seed bytes (populated into VFS at ASGI init). */
extern const pm_metal_asgi_static_file_t pm_metal_asgi_static_files[];
extern const unsigned pm_metal_asgi_static_file_count;

/* rel_path: "" / "/" / "index.html" / "css/base.css" (no mount prefix). */
const pm_metal_asgi_static_file_t *pm_metal_asgi_static_lookup(const char *rel_path);

#endif
