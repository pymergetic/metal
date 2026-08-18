/* pymergetic.metal.inspect — live registry JSON + ASGI routes. */
#include "pymergetic/metal/inspect/__exports__.h"

#include "pymergetic/metal/net/http/asgi.h"
#include "pymergetic/wasmmod/registry.h"

#include "www_embed.inc.h"

#include <stdlib.h>
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
    js_raw(j, "\"microdot\":true,\"fastapi\":false,\"utemplate\":true,");
    js_raw(j, "\"ssh_kex\":true,\"ssh_auth\":false,");
    js_raw(j, "\"vfs_static\":false,\"static_embed\":true,\"static_backend\":\"embed\",");
    js_raw(j, "\"rpc\":true,\"rpc_i64\":true,\"rpc_f32\":false,\"rpc_f64\":false}");
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

/* Parse `/inspect/reg/<fqn>` or `/inspect/reg/<fqn>/<func>` (or any two/three
 * trailing segments after `prefix`). Returns 1 + writes up to two segments
 * (fqn, func) if present. Module FQNs contain dots but never '/', so the
 * whole segment is the module. */
static int reg_split(const char *path, const char *prefix, char *fqn, uint32_t fqn_max,
    char *func, uint32_t func_max) {
    const char *p = path;
    size_t plen = strlen(prefix);
    size_t n;
    if (strncmp(path, prefix, plen) != 0) {
        return 0;
    }
    p = path + plen;
    if (*p == 0) {
        return 1; /* bare prefix: no module -> caller falls back to full reg */
    }
    if (*p == '/') {
        p++;
    }
    n = 0;
    while (*p != 0 && *p != '/' && n + 1u < fqn_max) {
        fqn[n++] = *p++;
    }
    fqn[n] = 0;
    if (*p == 0) {
        return 1;
    }
    if (*p == '/') {
        p++;
    }
    n = 0;
    while (*p != 0 && *p != '/' && n + 1u < func_max) {
        func[n++] = *p++;
    }
    func[n] = 0;
    return 2;
}

static int32_t export_of_sig(const char *fqn, uint32_t exp_index, char *buf, uint32_t buf_max,
    char *sig, uint32_t sig_max, pm_wasmmod_registry_export_kind_t *kind) {
    uint8_t fqnb[192];
    uint32_t flen = (uint32_t)strlen(fqn);
    uint8_t ename[128];
    uint32_t elen = sizeof(ename);
    uint8_t esig[192];
    uint32_t slen = sizeof(esig);
    if (flen >= sizeof(fqnb)) {
        return -1;
    }
    memcpy(fqnb, fqn, flen);
    if (pm_wasmmod_registry_export_at(fqnb, flen, exp_index, ename, &elen, kind, esig, &slen) == 0) {
        return -1;
    }
    if (elen == 0 || elen >= buf_max) {
        return -1;
    }
    memcpy(buf, ename, elen);
    buf[elen] = 0;
    if (slen < sig_max) {
        memcpy(sig, esig, slen);
        sig[slen] = 0;
    } else if (sig_max > 0) {
        sig[0] = 0;
    }
    return 0;
}

static int32_t export_of(const char *fqn, uint32_t exp_index, char *buf, uint32_t buf_max,
    uint32_t *out_len, pm_wasmmod_registry_export_kind_t *kind) {
    char dummy_sig[1];
    int32_t rc = export_of_sig(fqn, exp_index, buf, buf_max, dummy_sig, sizeof(dummy_sig), kind);
    if (rc == 0 && out_len != NULL) {
        *out_len = (uint32_t)strlen(buf);
    }
    return rc;
}

static const char *kind_tag(pm_wasmmod_registry_export_kind_t k) {
    switch (k) {
    case PM_WASMMOD_REGISTRY_EXPORT_FN:
        return "fn";
    case PM_WASMMOD_REGISTRY_EXPORT_MEM:
        return "mem";
    case PM_WASMMOD_REGISTRY_EXPORT_OBJ:
        return "obj";
    case PM_WASMMOD_REGISTRY_EXPORT_I64:
        return "i64";
    case PM_WASMMOD_REGISTRY_EXPORT_F32:
        return "f32";
    case PM_WASMMOD_REGISTRY_EXPORT_F64:
        return "f64";
    case PM_WASMMOD_REGISTRY_EXPORT_BUFPTR:
        return "bufptr";
    default:
        return "?";
    }
}

/* /inspect/reg/<fqn> — exports of one module. */
static void fill_exports(js_t *j, const char *fqn) {
    uint8_t fqnb[192];
    uint32_t flen = (uint32_t)strlen(fqn);
    uint32_t nx;
    uint32_t i;
    int found = 0;
    uint32_t nmod = 0;
    if (flen >= sizeof(fqnb)) {
        js_raw(j, "{\"error\":\"not_found\",\"module\":");
        js_str(j, fqn);
        js_ch(j, '}');
        return;
    }
    memcpy(fqnb, fqn, flen);
    nmod = pm_wasmmod_registry_module_count();
    for (i = 0; i < nmod; i++) {
        char name[192];
        if (mod_name(i, name, sizeof(name), NULL) == 0 && strcmp(name, fqn) == 0) {
            found = 1;
            break;
        }
    }
    if (!found) {
        js_raw(j, "{\"error\":\"not_found\",\"module\":");
        js_str(j, fqn);
        js_ch(j, '}');
        return;
    }
    nx = pm_wasmmod_registry_export_count(fqnb, flen);
    js_raw(j, "{\"module\":");
    js_str(j, fqn);
    js_raw(j, ",\"export_count\":");
    js_u32(j, nx);
    js_raw(j, ",\"exports\":[");
    for (i = 0; i < nx; i++) {
        char ename[128];
        char esig[192];
        pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
        if (i != 0) {
            js_ch(j, ',');
        }
        if (export_of_sig(fqn, i, ename, sizeof(ename), esig, sizeof(esig), &kind) == 0) {
            js_raw(j, "{\"name\":");
            js_str(j, ename);
            js_raw(j, ",\"kind\":\"");
            js_raw(j, kind_tag(kind));
            if (esig[0] != 0) {
                js_raw(j, "\",\"sig\":");
                js_str(j, esig);
            }
            js_ch(j, '}');
        } else {
            js_raw(j, "{\"name\":\"\"}");
        }
    }
    js_raw(j, "]}");
}

/* /inspect/reg/<fqn>/<func> — one export's detail. */
static void fill_export(js_t *j, const char *fqn, const char *func) {
    uint32_t nx;
    uint32_t i;
    int found = 0;
    char ename[128];
    pm_wasmmod_registry_export_kind_t kind = PM_WASMMOD_REGISTRY_EXPORT_FN;
    nx = pm_wasmmod_registry_export_count((const uint8_t *)fqn, (uint32_t)strlen(fqn));
    for (i = 0; i < nx && !found; i++) {
        if (export_of(fqn, i, ename, sizeof(ename), NULL, &kind) == 0 && strcmp(ename, func) == 0) {
            found = 1;
        }
    }
    js_raw(j, "{\"module\":");
    js_str(j, fqn);
    js_raw(j, ",\"func\":");
    js_str(j, func);
    js_raw(j, ",\"found\":");
    js_raw(j, found ? "true" : "false");
    if (found) {
        char esig[192];
        js_raw(j, ",\"export\":{\"name\":");
        js_str(j, func);
        js_raw(j, ",\"kind\":\"");
        js_raw(j, kind_tag(kind));
        if (export_of_sig(fqn, i - 1u, ename, sizeof(ename), esig, sizeof(esig), &kind) == 0
            && esig[0] != 0) {
            js_raw(j, "\",\"sig\":");
            js_str(j, esig);
        }
        js_ch(j, '}');
    }
    js_ch(j, '}');
}

/* /inspect/call/<fqn>/<func>?aN=<kind>:<val> — invoke one registry FN export
 * with up to INSPECT_ARGS_MAX scalar values, emit the result as JSON. */
#define PM_METAL_INSPECT_ARGS_MAX 8
#define PM_METAL_INSPECT_RESULTS_MAX 1

static int64_t atoi64(const char *s) {
    int64_t v = 0;
    int neg = 0;
    if (s == NULL) {
        return 0;
    }
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (int64_t)(*s - '0');
        s++;
    }
    return neg ? -v : v;
}

static pm_wasmmod_registry_value_t arg_from_query(const char *v, int *ok) {
    pm_wasmmod_registry_value_t x;
    const char *colon = v != NULL ? strchr(v, ':') : NULL;
    x.kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
    x.of.i32 = 0;
    *ok = 0;
    if (colon == NULL || colon == v) {
        return x;
    }
    /* Integer kinds only on the wire: strtod/strtof are not part of the
     * freestanding firmware libc. f32/f64 args return unsupported. */
    if (strncmp(v, "i32:", 4) == 0) {
        x.kind = PM_WASMMOD_REGISTRY_VALKIND_I32;
        x.of.i32 = (int32_t)strtol(colon + 1, NULL, 0);
        *ok = 1;
    } else if (strncmp(v, "i64:", 4) == 0) {
        x.kind = PM_WASMMOD_REGISTRY_VALKIND_I64;
        x.of.i64 = atoi64(colon + 1);
        *ok = 1;
    }
    return x;
}

static void js_i32(js_t *j, int32_t v) {
    if (v < 0) {
        js_ch(j, '-');
        js_u32(j, (uint32_t)(-(int64_t)v));
    } else {
        js_u32(j, (uint32_t)v);
    }
}

static void fill_call(js_t *j, const char *path) {
    char fqn[192];
    char func[128];
    char query[256];
    const char *q;
    uint8_t fqnb[192];
    uint8_t fnb[128];
    pm_wasmmod_registry_value_t args[PM_METAL_INSPECT_ARGS_MAX];
    pm_wasmmod_registry_value_t res[PM_METAL_INSPECT_RESULTS_MAX];
    uint32_t nargs = 0;
    int rc;
    q = strchr(path, '?');
    if (q != NULL) {
        size_t qlen = (size_t)(q - path);
        if (qlen < sizeof(query)) {
            memcpy(query, path, qlen);
            query[qlen] = 0;
        } else {
            query[0] = 0;
        }
        fqn[0] = 0;
        func[0] = 0;
        path = query;
    } else {
        fqn[0] = 0;
        func[0] = 0;
    }
    if (strncmp(path, "/inspect/call/", 14) != 0) {
        js_raw(j, "{\"error\":\"bad_path\"}");
        return;
    }
    path += 14;
    /* parse <fqn>/<func> (module may contain dots, never '/') */
    {
        const char *slash = strchr(path, '/');
        size_t n;
        if (slash == NULL) {
            js_raw(j, "{\"error\":\"need func\"}");
            return;
        }
        n = (size_t)(slash - path);
        if (n >= sizeof(fqn)) {
            n = sizeof(fqn) - 1;
        }
        memcpy(fqn, path, n);
        fqn[n] = 0;
        n = strlen(slash + 1);
        if (n >= sizeof(func)) {
            n = sizeof(func) - 1;
        }
        memcpy(func, slash + 1, n);
        func[n] = 0;
        if (q != NULL) {
            /* args from query string a0=..&a1=.. */
            const char *cur = q + 1;
            while (*cur != 0 && nargs < PM_METAL_INSPECT_ARGS_MAX) {
                const char *amp = strchr(cur, '&');
                const char *eq = strchr(cur, '=');
                int ok = 0;
                if (amp == NULL) {
                    amp = cur + strlen(cur);
                }
                if (eq != NULL && eq < amp) {
                    size_t klen = (size_t)(eq - cur);
                    char key[16];
                    size_t kn = klen < sizeof(key) ? klen : sizeof(key) - 1;
                    memcpy(key, cur, kn);
                    key[kn] = 0;
                    if (strncmp(key, "a", 1) == 0) {
                        char val[64];
                        size_t vn = (size_t)(amp - (eq + 1));
                        size_t vc = vn < sizeof(val) ? vn : sizeof(val) - 1;
                        memcpy(val, eq + 1, vc);
                        val[vc] = 0;
                        args[nargs] = arg_from_query(val, &ok);
                        if (ok) {
                            nargs++;
                        }
                    }
                }
                cur = amp + (*amp != 0 ? 1 : 0);
            }
        }
    }
    memcpy(fqnb, fqn, (uint32_t)strlen(fqn));
    memcpy(fnb, func, (uint32_t)strlen(func));
    /* RPC through pm_wasmmod_registry_call is only sound for value-ABI
     * trampolines that container (wasm/aot/elf) exports install. Resident C/Rust
     * exports are raw native pointers (guest.h stores impl_fn), so transmuting
     * them to the 4-arg value-ABI fn and calling would be type-confused/UB. Gate
     * them out explicitly. */
    {
        int32_t container = pm_wasmmod_registry_container(fqnb, (uint32_t)strlen(fqn));
        if (container >= (int32_t)PM_WASMMOD_REGISTRY_CONTAINER_RESIDENT
            || container < (int32_t)PM_WASMMOD_REGISTRY_CONTAINER_WASM) {
            js_raw(j, "{\"module\":");
            js_str(j, fqn);
            js_raw(j, ",\"func\":");
            js_str(j, func);
            js_raw(j, ",\"status\":1,\"error\":\"native_module\"");
            js_raw(j, ",\"detail\":\"rpc only for container (wasm/aot/elf) exports\"}");
            return;
        }
    }
    rc = pm_wasmmod_registry_call(fqnb, (uint32_t)strlen(fqn), fnb, (uint32_t)strlen(func), args,
        nargs, res, PM_METAL_INSPECT_RESULTS_MAX);
    js_raw(j, "{\"module\":");
    js_str(j, fqn);
    js_raw(j, ",\"func\":");
    js_str(j, func);
    js_raw(j, ",\"status\":");
    js_u32(j, rc == 0 ? 0u : 1u);
    if (rc != 0) {
        js_raw(j, ",\"error\":\"call_failed\"");
        js_ch(j, '}');
        return;
    }
    js_raw(j, ",\"result\":{\"kind\":\"");
    switch (res[0].kind) {
    case PM_WASMMOD_REGISTRY_VALKIND_I32:
        js_raw(j, "i32\"");
        break;
    case PM_WASMMOD_REGISTRY_VALKIND_I64:
        js_raw(j, "i64\"");
        break;
    case PM_WASMMOD_REGISTRY_VALKIND_F32:
        js_raw(j, "f32\"");
        break;
    case PM_WASMMOD_REGISTRY_VALKIND_F64:
        js_raw(j, "f64\"");
        break;
    default:
        js_raw(j, "?\"");
        break;
    }
    js_raw(j, ",\"value\":");
    switch (res[0].kind) {
    case PM_WASMMOD_REGISTRY_VALKIND_I32:
        js_i32(j, res[0].of.i32);
        break;
    default:
        /* The value ABI carries i64/f32/f64 too, but printing them needs
         * 64-bit division or %g/strtod, neither guaranteed in the
         * freestanding firmware libc. They come back as null. The kind tag
         * above still tells the caller what the registry actually returned. */
        js_raw(j, "null");
        break;
    }
    js_ch(j, '}');
    js_ch(j, '}');
}

static int32_t fill(const char *method, const char *path, char *out, uint32_t out_max) {
    js_t j;
    const char *raw;
    if (out == NULL || out_max < 2) {
        return -1;
    }
    out[0] = 0;
    j.p = out;
    j.n = 0;
    j.max = out_max;
    raw = path != NULL ? path : "";
    path = path_only(raw);
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
        fill_reg(&j, path_is(path, "/inspect/reg/completeness") && path_has(raw, "fmt=tree"));
        return js_ok(&j) ? 200 : -1;
    }
    if (path_is(path, "/inspect/reg/seats")) {
        js_raw(&j, "{\"schema\":1,\"seats\":[\"this\"],\"note\":\"this_seat_registry\"}");
        return js_ok(&j) ? 200 : -1;
    }
    if (strncmp(path, "/inspect/reg/", 13) == 0) {
        /* /inspect/reg/<fqn> (exports) or /inspect/reg/<fqn>/<func> (detail).
         * Base /inspect/reg and /inspect/reg/completeness/seats taken above. */
        if (strncmp(path, "/inspect/reg/completeness", 25) != 0) {
            char fqn[192];
            char func[128];
            int parts;
            fqn[0] = 0;
            func[0] = 0;
            parts = reg_split(path, "/inspect/reg/", fqn, sizeof(fqn), func, sizeof(func));
            if (parts >= 1 && fqn[0] != 0) {
                if (parts >= 2 && func[0] != 0) {
                    fill_export(&j, fqn, func);
                } else {
                    fill_exports(&j, fqn);
                }
                return js_ok(&j) ? 200 : -1;
            }
        }
    }
    if (strncmp(path, "/inspect/call/", 14) == 0) {
        fill_call(&j, raw);
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
    /* Wildcard-prefix routes: the C handler parses the trailing segments for
     * /inspect/reg/<fqn> (exports) and /inspect/call/<fqn>/<func> (RPC). */
    (void)add_route("/inspect/reg/*");
    (void)add_route("/inspect/call/*");
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
