/* pymergetic.metal.inspect — live registry JSON + ASGI routes. */
#include "pymergetic/metal/inspect/__exports__.h"

#include "pymergetic/metal/net/http/asgi.h"
#include "pymergetic/wasmmod/registry.h"

#include "www_embed.inc.h"

#include <string.h>

#ifndef PM_METAL_INSPECT_BODY
#define PM_METAL_INSPECT_BODY 16384u
#endif
#ifndef PM_METAL_INSPECT_MOD_MAX
#define PM_METAL_INSPECT_MOD_MAX 256u
#endif

static char s_body[PM_METAL_INSPECT_BODY];
static int32_t s_status;

typedef struct {
    char *p;
    uint32_t n;
    uint32_t max;
} js_t;

static int js_ok(const js_t *j) {
    return j->n < j->max;
}

static void js_ch(js_t *j, char c) {
    if (j->n + 1u < j->max) {
        j->p[j->n++] = c;
        j->p[j->n] = 0;
    } else {
        j->n = j->max;
    }
}

static void js_raw(js_t *j, const char *s) {
    while (s != NULL && *s != 0) {
        js_ch(j, *s++);
    }
}

static void js_u32(js_t *j, uint32_t v) {
    char tmp[10];
    uint32_t i = 0;
    if (v == 0) {
        js_ch(j, '0');
        return;
    }
    while (v != 0 && i < sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i > 0) {
        js_ch(j, tmp[--i]);
    }
}

static void js_str(js_t *j, const char *s) {
    js_ch(j, '"');
    while (s != NULL && *s != 0) {
        char c = *s++;
        if (c == '"' || c == '\\') {
            js_ch(j, '\\');
        }
        if ((unsigned char)c < 0x20) {
            continue;
        }
        js_ch(j, c);
    }
    js_ch(j, '"');
}

static const char *path_only(const char *path) {
    return path != NULL ? path : "";
}

static int path_is(const char *path, const char *want) {
    const char *q;
    size_t n;
    if (path == NULL || want == NULL) {
        return 0;
    }
    q = strchr(path, '?');
    n = q != NULL ? (size_t)(q - path) : strlen(path);
    return n == strlen(want) && memcmp(path, want, n) == 0;
}

static int path_has(const char *path, const char *needle) {
    return path != NULL && needle != NULL && strstr(path, needle) != NULL;
}

static int32_t mod_name(uint32_t i, char *buf, uint32_t buf_max, uint32_t *out_len) {
    uint8_t raw[192];
    uint32_t len = (uint32_t)sizeof(raw);
    if (pm_wasmmod_registry_module_at(i, raw, &len) == 0 || len == 0 || len >= buf_max) {
        return -1;
    }
    memcpy(buf, raw, len);
    buf[len] = 0;
    if (out_len != NULL) {
        *out_len = len;
    }
    return 0;
}

static int name_cmp(uint32_t a, uint32_t b) {
    char na[192];
    char nb[192];
    if (mod_name(a, na, sizeof(na), NULL) != 0) {
        return 1;
    }
    if (mod_name(b, nb, sizeof(nb), NULL) != 0) {
        return -1;
    }
    return strcmp(na, nb);
}

static uint32_t sorted_ids(uint16_t *idx, uint32_t idx_max) {
    uint32_t nmod = pm_wasmmod_registry_module_count();
    uint32_t n = nmod < idx_max ? nmod : idx_max;
    uint32_t i;
    for (i = 0; i < n; i++) {
        idx[i] = (uint16_t)i;
    }
    for (i = 1; i < n; i++) {
        uint16_t v = idx[i];
        uint32_t j = i;
        while (j > 0 && name_cmp(idx[j - 1], v) > 0) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = v;
    }
    return n;
}

static void fill_self(js_t *j) {
    js_raw(j, "{\"schema\":1,\"name\":\"pymergetic.metal\",\"role\":\"kernel\",");
    js_raw(j, "\"product\":\"metal\",\"org\":\"pymergetic\",\"theme\":\"metal\",");
    js_raw(j, "\"has_source\":false,\"has_pack\":true,\"static_backend\":\"embed\",");
    js_raw(j, "\"source_files\":[],\"pack_files\":[\"httpd.json\"],");
    js_raw(j, "\"tags\":{\"role\":\"kernel\",\"product\":\"metal\",\"org\":\"pymergetic\"}}");
}

static void fill_caps(js_t *j) {
    js_raw(j, "{\"role\":\"metal\",\"theme\":\"metal\",\"asgi\":true,");
    js_raw(j, "\"microdot\":true,\"fastapi\":false,\"ssh_kex\":true,\"ssh_auth\":false,");
    js_raw(j, "\"vfs_static\":false,\"static_embed\":true,\"static_backend\":\"embed\"}");
}

static void fill_reg(js_t *j, int tree) {
    uint16_t idx[PM_METAL_INSPECT_MOD_MAX];
    uint32_t n = sorted_ids(idx, PM_METAL_INSPECT_MOD_MAX);
    uint32_t i;
    if (tree) {
        js_raw(j, "registry  ");
        js_u32(j, n);
        js_raw(j, " module(s)\n");
        for (i = 0; i < n; i++) {
            char name[192];
            if (mod_name(idx[i], name, sizeof(name), NULL) != 0) {
                continue;
            }
            js_raw(j, "+-- ");
            js_raw(j, name);
            js_ch(j, '\n');
        }
        return;
    }
    js_raw(j, "{\"schema\":1,\"method_count\":");
    js_u32(j, n);
    js_raw(j, ",\"gap_count\":0,\"modules\":[");
    for (i = 0; i < n; i++) {
        char name[192];
        if (mod_name(idx[i], name, sizeof(name), NULL) != 0) {
            continue;
        }
        if (i != 0) {
            js_ch(j, ',');
        }
        js_str(j, name);
    }
    js_raw(j, "],\"methods\":[");
    for (i = 0; i < n; i++) {
        char name[192];
        uint32_t nlen = 0;
        uint32_t exports;
        if (mod_name(idx[i], name, sizeof(name), &nlen) != 0) {
            continue;
        }
        exports = pm_wasmmod_registry_export_count((const uint8_t *)name, nlen);
        if (i != 0) {
            js_ch(j, ',');
        }
        js_raw(j, "{\"module\":");
        js_str(j, name);
        js_raw(j, ",\"func\":\"*\",\"exports\":");
        js_u32(j, exports);
        js_ch(j, '}');
    }
    js_raw(j, "],\"gaps\":[],\"note\":\"live_registry\"");
    if (!tree) {
        js_raw(j, ",\"completeness_url\":\"/inspect/reg/completeness\"}");
    } else {
        js_ch(j, '}');
    }
}

static int32_t fill(const char *method, const char *path, char *out, uint32_t out_max) {
    js_t j;
    if (out == NULL || out_max < 2) {
        return -1;
    }
    out[0] = 0;
    j.p = out;
    j.n = 0;
    j.max = out_max;
    path = path_only(path);
    if (method == NULL || strcmp(method, "GET") != 0) {
        js_raw(&j, "{\"error\":\"method\"}");
        return js_ok(&j) ? 405 : -1;
    }
    if (path_is(path, "/health")) {
        js_raw(&j, "{\"ok\":true}");
        return js_ok(&j) ? 200 : -1;
    }
    if (path_is(path, "/capabilities")) {
        fill_caps(&j);
        return js_ok(&j) ? 200 : -1;
    }
    if (path_is(path, "/inspect/self")) {
        fill_self(&j);
        return js_ok(&j) ? 200 : -1;
    }
    if (path_is(path, "/inspect/reg") || path_is(path, "/inspect/reg/completeness")) {
        fill_reg(&j, path_is(path, "/inspect/reg/completeness") && path_has(path, "fmt=tree"));
        return js_ok(&j) ? 200 : -1;
    }
    if (path_is(path, "/inspect/reg/seats")) {
        js_raw(&j, "{\"schema\":1,\"seats\":[\"this\"],\"note\":\"this_seat_registry\"}");
        return js_ok(&j) ? 200 : -1;
    }
    js_raw(&j, "{\"error\":\"not_found\"}");
    return js_ok(&j) ? 404 : -1;
}

int32_t pm_metal_inspect_handle(const char *method, const char *path) {
    s_status = fill(method, path, s_body, sizeof(s_body));
    return s_status;
}

const char *pm_metal_inspect_body(void) {
    return s_body;
}

static int32_t asgi_handler(const char *method, const char *path, uint8_t *out, uint32_t out_max,
    uint32_t *out_len) {
    int32_t st;
    if (out == NULL || out_len == NULL || out_max < 2) {
        return -1;
    }
    st = fill(method, path, (char *)out, out_max);
    if (st < 0) {
        return -1;
    }
    *out_len = (uint32_t)strlen((const char *)out);
    return 0;
}

static int32_t add_route(const char *path) {
    return pm_metal_net_http_asgi_route_fn("GET", path, asgi_handler);
}

int32_t pm_metal_inspect_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    s_body[0] = 0;
    s_status = 0;
    (void)add_route("/health");
    (void)add_route("/capabilities");
    (void)add_route("/inspect/self");
    (void)add_route("/inspect/reg");
    (void)add_route("/inspect/reg/completeness");
    (void)add_route("/inspect/reg/seats");
    if (inspect_www_mount() != 0) {
        return -1;
    }
    return 0;
}

void pm_metal_inspect_deinit(void) {
    s_body[0] = 0;
    s_status = 0;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.inspect, pm_metal_inspect_init, pm_metal_inspect_init,
    int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.inspect, pm_metal_inspect_deinit, pm_metal_inspect_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.inspect, pm_metal_inspect_handle, pm_metal_inspect_handle,
    int32_t(const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.inspect, pm_metal_inspect_body, pm_metal_inspect_body, const char *(void));

PM_MOD_BOOT_C(pymergetic.metal.inspect, pm_metal_inspect_init, pm_metal_inspect_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.inspect, pymergetic.metal.net.http.asgi);
