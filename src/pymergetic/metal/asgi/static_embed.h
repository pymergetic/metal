#ifndef PM_METAL_ASGI_STATIC_EMBED_H_
#define PM_METAL_ASGI_STATIC_EMBED_H_

#include <stdint.h>

typedef struct {
    const char *path; /* relative under /inspect, e.g. index.html */
    const char *ctype;
    const uint8_t *data;
    uint32_t len;
} pm_metal_asgi_static_file_t;

/* rel_path: "" / "/" / "index.html" / "css/base.css" (no /inspect prefix). */
const pm_metal_asgi_static_file_t *pm_metal_asgi_static_lookup(const char *rel_path);

#endif
