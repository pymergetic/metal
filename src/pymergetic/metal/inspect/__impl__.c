/* pymergetic.metal.inspect — live registry JSON + ASGI routes. */
#include "pymergetic/metal/inspect/__exports__.h"

#include "pymergetic/metal/net/http/asgi.h"
#include "pymergetic/metal/build/__types__.h"
#include "pymergetic/util/mem.h"
#include "pymergetic/wasmmod/registry.h"

#include "www_embed.inc.h"
#include "src_embed.inc.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if !defined(PM_METAL_FIRMWARE) && !defined(PM_METAL_BROWSER)
#include <sys/stat.h>
#endif

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
    js_raw(j, "\"zenoh\":true,");
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

static int32_t fill(const char *method, const char *path, char *out, uint32_t out_max);

/* /changes/<target> — the ledger read pane (defined after fill; the local
 * dispatch in fill and the asgi route both serve it). */
static int32_t changes_http(const char *method, const char *path,
    char *out, uint32_t out_max, uint32_t *out_len);

/* Build face handlers (defined after fill; same relay pattern). */
static int32_t build_http(const char *method, const char *path,
    char *out, uint32_t out_max, uint32_t *out_len);
static int32_t build_index_http(const char *method, const char *path,
    char *out, uint32_t out_max, uint32_t *out_len);
static int32_t build_rebuild_http(const char *method, const char *path,
    char *out, uint32_t out_max, uint32_t *out_len);

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
    if (method == NULL
        || (strcmp(method, "GET") != 0
            && !(strcmp(method, "POST") == 0 && strncmp(path, "/build/", 7) == 0))) {
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
    /* /docs/<fqn>/<name> — doc extract for one export face. Same body the
     * /docs asgi route serves; this local path is what REPL callers and
     * the host prove hit without a listener. */
    if (strncmp(path, "/docs/", 6) == 0) {
        const char *p = path + 6;
        const char *slash = strchr(p, '/');
        char fqnbuf[192];
        size_t flen;
        const char *doc;
        if (slash == NULL) {
            js_raw(&j, "{\"error\":\"not_found\"}");
            return 404;
        }
        flen = (size_t)(slash - p);
        if (flen == 0 || flen >= sizeof(fqnbuf)) {
            js_raw(&j, "{\"error\":\"not_found\"}");
            return 404;
        }
        memcpy(fqnbuf, p, flen);
        fqnbuf[flen] = 0;
        doc = pm_metal_inspect_doc(fqnbuf, slash + 1);
        if (doc == NULL) {
            js_raw(&j, "{\"error\":\"not_found\"}");
            return 404;
        }
        js_raw(&j, doc);
        return js_ok(&j) ? 200 : -1;
    }
    /* /changes/<target> — the ledger read pane, same body the /changes asgi
     * route serves; local path for REPL + host prove without a listener. */
    if (strncmp(path, "/changes/", 9) == 0) {
        uint32_t blen = 0;
        if (changes_http(method, path, j.p, j.max, &blen) == 0) {
            j.n = blen;
            return 200;
        }
        js_raw(&j, "{\"error\":\"not_found\"}");
        return 404;
    }
    /* /build — the tree index (GET) and /build/<fqn> rebuild (POST): same
     * bodies the asgi routes serve, local for the host prove + REPL. The
     * rebuild is the whole in-kernel chain, so it needs the bigger body
     * buffer than s_body provides for records — it writes through its own
     * out pointer, and fill just relays the length. */
    if (path_is(path, "/build")) {
        uint32_t blen = 0;
        if (build_index_http(method, path, j.p, j.max, &blen) == 0) {
            j.n = blen;
            return 200;
        }
        js_raw(&j, "{\"error\":\"not_found\"}");
        return 404;
    }
    if (strncmp(path, "/build/", 7) == 0) {
        uint32_t blen = 0;
        if (build_rebuild_http(method, path, j.p, j.max, &blen) == 0) {
            j.n = blen;
            return 200;
        }
        if (build_http(method, path, j.p, j.max, &blen) == 0) {
            j.n = blen;
            return 200;
        }
        js_raw(&j, "{\"error\":\"not_found\"}");
        return 404;
    }
    js_raw(&j, "{\"error\":\"not_found\"}");
    return js_ok(&j) ? 404 : -1;
}

/* Serve one inspect route locally, without a listener.
 *
 * :param method: "GET" (other methods are 405)
 * :param path: route path, e.g. "/inspect/self" or "/docs/<fqn>/<fn>"
 * :return: HTTP status code; the body comes from pm_metal_inspect_body()
 * :example:
 * st = pm_metal_inspect_handle("GET", "/health");
 * body = pm_metal_inspect_body();  // {"ok":true}
 */
int32_t pm_metal_inspect_handle(const char *method, const char *path) {
    s_status = fill(method, path, s_body, sizeof(s_body));
    return s_status;
}

const char *pm_metal_inspect_body(void) {
    return s_body;
}

/* ===== Card source tree (tools/embed_src.py -> src_embed.inc.h) =====
 *
 * The source pane is real code, embedded from each card's authored .c/.rs at
 * build and served from the same arrays on every seat (incl. firmware). These
 * faces are what the Python artifacts pump calls for /files and /files/raw;
 * the /src/<fqn> and /src/<fqn>/<file> asgi routes make the same bytes
 * fetchable directly, so the host C prove can hit them over HTTP too. */

/* The card's embedded source manifest (JSON: name + files[] with raw_len).
 *
 * :param fqn: fully-qualified card name, e.g. "pymergetic.metal.jit.c"
 * :return: manifest JSON or NULL when the card has no muscle source
 * :example:
 * man = pm_metal_inspect_src_manifest("pymergetic.metal.inspect");
 */
const char *pm_metal_inspect_src_manifest(const char *fqn) {
    const pm_metal_src_card_t *c = fqn != NULL ? pm_metal_src_find(fqn) : NULL;
    return c != NULL ? c->manifest : NULL;
}

const char *pm_metal_inspect_src_read(const char *fqn, const char *path) {
    const pm_metal_src_card_t *c;
    uint32_t i;
    if (fqn == NULL || path == NULL) {
        return NULL;
    }
    c = pm_metal_src_find(fqn);
    if (c == NULL) {
        return NULL;
    }
    for (i = 0; i < c->nfiles; i++) {
        if (strcmp(c->files[i].rel, path) == 0) {
            return (const char *)c->files[i].data;
        }
    }
    return NULL;
}

/* ===== Docs extraction (Phase 9) =====
 *
 * The doc bytes already ship in the embedded card table — the extractor is
 * the missing piece. For an export face it finds the PM_MOD_EXPORT_C/_RS
 * line naming it, walks back over the contiguous comment block, and renders
 * it as JSON: prose, :param name: entries, and an :example: block. Rust
 * cards use /// doc comments above the #[unsafe(no_mangle)] export, so the
 * walker accepts both block comments (C) and /// runs (Rust). */

#define PM_INSPECT_DOC_MAX (PM_METAL_INSPECT_BODY / 2u)
#define PM_INSPECT_DOC_PROSE_MAX 2048u
#define PM_INSPECT_DOC_PARAMS_MAX 12u
#define PM_INSPECT_DOC_PARAM_MAX 96u

/* Locate the export macro line for (fqn, name) inside file text. The macro
 * sits in the export block at the file's foot, where no doc comment lives;
 * the authored doc is above the function DEFINITION. The macro's second
 * field repeats the C name, so find the macro (for existence + line
 * reporting), then locate the definition line "name(" at a line start and
 * hand doc_collect the definition's offset. macro_out gets the definition
 * offset when found, else the macro offset. Returns the offset or (size_t)-1
 * when the face is not exported from this file. */
static size_t doc_find_export(const char *text, size_t len,
    const char *fqn, const char *name, size_t *macro_off) {
    char needle[192];
    char defn[96];
    size_t nl;
    size_t dl;
    size_t i;
    size_t macro_at = (size_t)-1;
    nl = (size_t)snprintf(needle, sizeof(needle), "PM_MOD_EXPORT_C(%s, %s,", fqn, name);
    if (nl > 0 && nl < sizeof(needle)) {
        for (i = 0; i + nl <= len; i++) {
            if (text[i] != needle[0] || memcmp(text + i, needle, nl) != 0) {
                continue;
            }
            if (i == 0 || text[i - 1] == '\n') {
                macro_at = i;
                break;
            }
        }
    }
    if (macro_at == (size_t)-1) {
        /* Rust: the export macro is PM_MOD_EXPORT_RS!("fqn", name, ...) */
        nl = (size_t)snprintf(needle, sizeof(needle), "PM_MOD_EXPORT_RS!(\"%s\", %s,", fqn, name);
        if (nl <= 0 || nl >= sizeof(needle)) {
            return (size_t)-1;
        }
        for (i = 0; i + nl <= len; i++) {
            if (text[i] != needle[0] || memcmp(text + i, needle, nl) != 0) {
                continue;
            }
            if (i == 0 || text[i - 1] == '\n') {
                macro_at = i;
                break;
            }
        }
        if (macro_at == (size_t)-1) {
            return (size_t)-1;
        }
    }
    /* the definition: "name(" preceded on its line by nothing or by a single
     * return-type word ("int32_t name("). A call site ("x = name(",
     * "return name(") has '=' or two words before the name and never
     * matches. */
    dl = (size_t)snprintf(defn, sizeof(defn), "%s(", name);
    for (i = 0; i + dl <= len; i++) {
        size_t ls;
        size_t wstart;
        if (text[i] != name[0] || memcmp(text + i, defn, dl) != 0) {
            continue;
        }
        if (i == 0 || text[i - 1] != ' ') {
            continue;
        }
        ls = i;
        while (ls > 0 && text[ls - 1] != '\n') {
            ls--;
        }
        wstart = i - 1;
        while (wstart > ls && text[wstart - 1] != ' ') {
            wstart--;
        }
        /* the prefix [ls, i) must be exactly one type word + the separator
         * space — "int32_t name(". A call site has "= ", "return ", or a
         * second word before the name and fails this. */
        if (memchr(text + ls, ' ', i - ls - 1u) == NULL
            && memchr(text + ls, '=', i - ls) == NULL
            && memchr(text + ls, '(', i - ls) == NULL
            && memchr(text + ls, ')', i - ls) == NULL) {
            *macro_off = i;
            return i;
        }
    }
    *macro_off = macro_at;
    return macro_at;
}

typedef struct {
    char prose[PM_INSPECT_DOC_PROSE_MAX];
    uint32_t prose_len;
    char params[PM_INSPECT_DOC_PARAMS_MAX][PM_INSPECT_DOC_PARAM_MAX];
    uint32_t n_params;
    char example[512];
    uint32_t example_len;
    int has_any;
} pm_inspect_doc_t;

static void doc_push_prose(pm_inspect_doc_t *d, const char *s, size_t n) {
    if (d->prose_len + n + 1u >= sizeof(d->prose)) {
        n = sizeof(d->prose) - 1u - d->prose_len;
    }
    if (n == 0) {
        return;
    }
    memcpy(d->prose + d->prose_len, s, n);
    d->prose_len += (uint32_t)n;
    if (d->prose_len < sizeof(d->prose)) {
        d->prose[d->prose_len] = 0;
    }
}

static void doc_push_example(pm_inspect_doc_t *d, const char *s, size_t n) {
    if (d->example_len + n + 1u >= sizeof(d->example)) {
        n = sizeof(d->example) - 1u - d->example_len;
    }
    if (n == 0) {
        return;
    }
    memcpy(d->example + d->example_len, s, n);
    d->example_len += (uint32_t)n;
    if (d->example_len < sizeof(d->example)) {
        d->example[d->example_len] = 0;
    }
}

static void doc_push_param(pm_inspect_doc_t *d, const char *name, size_t nname,
    const char *desc, size_t ndesc) {
    size_t total;
    if (d->n_params >= PM_INSPECT_DOC_PARAMS_MAX || nname == 0) {
        return;
    }
    total = nname + 2u + ndesc;   /* "name: desc" */
    if (total >= PM_INSPECT_DOC_PARAM_MAX) {
        ndesc = PM_INSPECT_DOC_PARAM_MAX - 1u - nname - 2u;
    }
    memcpy(d->params[d->n_params], name, nname);
    d->params[d->n_params][nname] = ':';
    d->params[d->n_params][nname + 1u] = ' ';
    if (ndesc != 0) {
        memcpy(d->params[d->n_params] + nname + 2u, desc, ndesc);
    }
    d->params[d->n_params][nname + 2u + ndesc] = 0;
    d->n_params++;
    d->has_any = 1;
}

/* Walk one comment block (delimiters stripped, passed as raw comment body)
 * into the doc struct. A line starting ":param NAME:"
 * becomes a param entry; the ":example:" keyword starts the example block
 * (every following line until the block ends joins it, prose stops). */
static void doc_parse_lines(pm_inspect_doc_t *d, const char *block, size_t blen,
    int in_example) {
    size_t i = 0;
    while (i < blen) {
        size_t eol = i;
        const char *ln;
        size_t lnlen;
        while (eol < blen && block[eol] != '\n') {
            eol++;
        }
        ln = block + i;
        lnlen = eol - i;
        /* strip one leading run of block-comment continuation + spaces */
        while (lnlen > 0 && (*ln == ' ' || *ln == '\t' || *ln == '*')) {
            ln++;
            lnlen--;
        }
        while (lnlen > 0 && (ln[lnlen - 1] == ' ' || ln[lnlen - 1] == '\t'
            || ln[lnlen - 1] == '\r')) {
            lnlen--;
        }
        if (lnlen > 7u && memcmp(ln, ":param ", 7) == 0) {
            const char *p = ln + 7;
            size_t pn = 0;
            while (p + pn < ln + lnlen && p[pn] != ':' && p[pn] != ' ') {
                pn++;
            }
            if (pn != 0 && p + pn < ln + lnlen && p[pn] == ':') {
                size_t ds = pn + 1;
                while (p + ds < ln + lnlen && (p[ds] == ' ' || p[ds] == '\t')) {
                    ds++;
                }
                doc_push_param(d, p, pn, p + ds, lnlen - 7u - ds);
            }
        } else if (lnlen >= 9u && memcmp(ln, ":example:", 9) == 0) {
            in_example = 1;
            if (lnlen > 9u) {
                doc_push_example(d, ln + 9, lnlen - 9u);
                doc_push_example(d, "\n", 1);
            }
        } else if (in_example) {
            doc_push_example(d, ln, lnlen);
            doc_push_example(d, "\n", 1);
        } else if (lnlen != 0) {
            if (d->prose_len != 0) {
                doc_push_prose(d, " ", 1);
            }
            doc_push_prose(d, ln, lnlen);
            d->has_any = 1;
        }
        i = eol + 1;
    }
}

/* Extract the contiguous doc block above macro_off: for C, a block comment
 * whose close is the last non-blank thing before the macro; for
 * Rust, a /// run. Returns 1 when a block was found. */
static int doc_collect(const char *text, size_t len, size_t macro_off,
    pm_inspect_doc_t *d) {
    /* back over blanks between comment and the definition */
    size_t end = macro_off;
    (void)len;
    while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == ' '
        || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        end--;
    }
    /* back over the return-type word ("int32_t name(" — the offset names
     * the definition's identifier, the comment sits above the whole line) */
    while (end > 0 && (text[end - 1] != '\n' && text[end - 1] != ' '
        && text[end - 1] != '\t' && text[end - 1] != ';' && text[end - 1] != '}')) {
        end--;
    }
    while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == ' '
        || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        end--;
    }
    if (end == 0) {
        return 0;
    }
    /* Rust /// run: walk back over consecutive lines starting with /// */
    {
        size_t e = end;
        size_t start = e;
        int found = 0;
        while (e > 0) {
            size_t ls = e - 1;
            while (ls > 0 && text[ls] != '\n') {
                ls--;
            }
            if (text[ls] == '\n') {
                ls++;
            }
            /* line [ls, e) */
            size_t ll = e - ls;
            size_t k = ls;
            while (k < e && (text[k] == ' ' || text[k] == '\t')) {
                k++;
            }
            if (ll >= 3u && text[k] == '/' && text[k + 1] == '/' && text[k + 2] == '/') {
                start = ls;
                e = ls > 0 ? ls - 1 : 0;
                if (ls == 0) {
                    break;
                }
                /* e now points at the char before the line start (the \n) */
                found = 1;
                continue;
            }
            break;
        }
        if (found) {
            doc_parse_lines(d, text + start, end - start, 0);
            return 1;
        }
    }
    /* C block comment must end exactly at `end` */
    if (end >= 2u && text[end - 1] == '/' && text[end - 2] == '*') {
        size_t close = end;
        size_t i;
        /* scan backwards for the nearest "slash-star"; C comments do not
         * nest, and any inner "*" cannot be preceded by "/" here without
         * closing early — first hit scanning back is the open */
        for (i = close; i >= 2u; i--) {
            if (text[i - 2] == '/' && text[i - 1] == '*') {
                size_t body = i;
                size_t blen = close - 2u - body;
                doc_parse_lines(d, text + body, blen, 0);
                return 1;
            }
        }
    }
    return 0;
}

static int32_t doc_render_json(const char *fqn, const char *name,
    const char *file, uint32_t line, const char *impl, const pm_inspect_doc_t *d,
    char *out, uint32_t out_max, uint32_t *out_len) {
    js_t j;
    uint32_t i;
    j.p = out;
    j.n = 0;
    j.max = out_max;
    js_raw(&j, "{\"fqn\":");
    js_str(&j, fqn);
    js_raw(&j, ",\"name\":");
    js_str(&j, name);
    js_raw(&j, ",\"file\":");
    js_str(&j, file);
    js_raw(&j, ",\"line\":");
    js_u32(&j, line);
    js_raw(&j, ",\"impl\":");
    js_str(&j, impl);
    js_raw(&j, ",\"prose\":");
    js_str(&j, d->prose);
    js_raw(&j, ",\"params\":[");
    for (i = 0; i < d->n_params; i++) {
        if (i > 0) {
            js_ch(&j, ',');
        }
        js_str(&j, d->params[i]);
    }
    js_raw(&j, "],\"example\":");
    js_str(&j, d->example);
    js_ch(&j, '}');
    if (!js_ok(&j)) {
        return -1;
    }
    *out_len = j.n;
    return 0;
}

/* Count the line (1-based) of offset off in text. */
static uint32_t doc_line_of(const char *text, size_t off) {
    uint32_t line = 1;
    size_t i;
    for (i = 0; i < off && text[i] != 0; i++) {
        if (text[i] == '\n') {
            line++;
        }
    }
    return line;
}

/* The public face: JSON doc for (fqn, name) or NULL when the face is not an
 * exported doc target (unknown fqn/name, or no doc block found). Static
 * buffer: single-threaded prove/REPL use; the route re-renders per request. */
static char s_doc_json[PM_INSPECT_DOC_MAX];

/* The :example: block alone, as runnable REPL text (no JSON wrapper). NULL
 * when the face has no doc or no example. Static buffer: same single-
 * threaded prove/REPL contract as pm_metal_inspect_doc. */
static char s_doc_example[512];

const char *pm_metal_inspect_example(const char *fqn, const char *name) {
    const pm_metal_src_card_t *c;
    pm_inspect_doc_t doc;
    uint32_t i;
    if (fqn == NULL || name == NULL) {
        return NULL;
    }
    c = pm_metal_src_find(fqn);
    if (c == NULL) {
        return NULL;
    }
    for (i = 0; i < c->nfiles; i++) {
        const char *text = (const char *)c->files[i].data;
        size_t len = c->files[i].len;
        size_t macro_off = 0;
        if (doc_find_export(text, len, fqn, name, &macro_off) != (size_t)-1) {
            memset(&doc, 0, sizeof(doc));
            if (!doc_collect(text, len, macro_off, &doc)) {
                return NULL;
            }
            if (doc.example_len == 0 || doc.example[0] == 0) {
                return NULL;
            }
            snprintf(s_doc_example, sizeof(s_doc_example), "%s", doc.example);
            return s_doc_example;
        }
    }
    return NULL;
}

/* The extracted doc for one export face, as JSON (prose + params + example).
 *
 * Walks the card's embedded muscle source, finds the export macro line naming
 * the face, and extracts the comment block above it. The same extractor the
 * /docs/<fqn>/<fn> route serves — REPL help and HTTP agree by construction.
 *
 * :param fqn: fully-qualified card name, e.g. "pymergetic.metal.jit.c"
 * :param name: export face name, e.g. "pm_metal_jit_c_object_compile"
 * :return: JSON doc string or NULL when fqn/name is not an exported face
 * :example:
 * doc = pm_metal_inspect_doc("pymergetic.metal.inspect",
 *     "pm_metal_inspect_handle");
 */
const char *pm_metal_inspect_doc(const char *fqn, const char *name) {
    const pm_metal_src_card_t *c;
    pm_inspect_doc_t doc;
    uint32_t i;
    const char *impl = "";
    if (fqn == NULL || name == NULL) {
        return NULL;
    }
    c = pm_metal_src_find(fqn);
    if (c == NULL) {
        return NULL;
    }
    impl = c->impl != NULL ? c->impl : "";
    for (i = 0; i < c->nfiles; i++) {
        const char *text = (const char *)c->files[i].data;
        size_t len = c->files[i].len;
        size_t macro_off = 0;
        if (doc_find_export(text, len, fqn, name, &macro_off) != (size_t)-1) {
            memset(&doc, 0, sizeof(doc));
            if (doc_collect(text, len, macro_off, &doc)) {
                uint32_t out_len = 0;
                if (doc_render_json(fqn, name, c->files[i].rel,
                        doc_line_of(text, macro_off), impl, &doc,
                        s_doc_json, sizeof(s_doc_json), &out_len) == 0) {
                    return s_doc_json;
                }
                return NULL;
            }
            /* export found but no doc block: empty doc, still 200 — the
             * face is real, its author wrote no comment above it */
            memset(&doc, 0, sizeof(doc));
            {
                uint32_t out_len = 0;
                if (doc_render_json(fqn, name, c->files[i].rel,
                        doc_line_of(text, macro_off), impl, &doc,
                        s_doc_json, sizeof(s_doc_json), &out_len) == 0) {
                    return s_doc_json;
                }
            }
            return NULL;
        }
    }
    return NULL;
}

static const char *src_ctype(const char *path) {
    const char *dot = path != NULL ? strrchr(path, '.') : NULL;
    if (dot == NULL) {
        return "text/plain; charset=utf-8";
    }
    if (strcmp(dot, ".c") == 0 || strcmp(dot, ".h") == 0) {
        return "text/x-c; charset=utf-8";
    }
    if (strcmp(dot, ".rs") == 0) {
        return "text/x-rust; charset=utf-8";
    }
    if (strcmp(dot, ".toml") == 0) {
        return "text/toml; charset=utf-8";
    }
    return "text/plain; charset=utf-8";
}

/* /src/<fqn>            -> manifest JSON.
 * /src/<fqn>/<file>     -> raw file body. Returns 0 and fills out on success,
 *                          -1 (-> asgi 404 empty) on an unknown fqn/file. */
static int32_t src_http(const char *method, const char *path, uint8_t *out, uint32_t out_max,
    uint32_t *out_len, const char **ctype) {
    const char *p;
    const char *fqn;
    const char *file;
    const char *body;
    size_t n;
    if (method == NULL || strcmp(method, "GET") != 0 || path == NULL) {
        return -1;
    }
    p = path;
    if (strncmp(p, "/src/", 5) != 0) {
        return -1;
    }
    p += 5;
    fqn = p;
    file = strchr(p, '/');
    if (file == NULL) {
        /* /src/<fqn> — manifest. */
        body = pm_metal_inspect_src_manifest(fqn);
        if (body == NULL) {
            return -1;
        }
        *ctype = "application/json";
        n = (uint32_t)strlen(body);
        if (n >= out_max) {
            return -1;
        }
        memcpy(out, body, n);
        *out_len = n;
        return 0;
    }
    /* /src/<fqn>/<file> — raw body into a NUL-terminated copy (the embedded
     * arrays are NUL-terminated, so a straight string length is the file len). */
    {
        char fqnbuf[192];
        size_t flen = (size_t)(file - fqn);
        if (flen >= sizeof(fqnbuf)) {
            return -1;
        }
        memcpy(fqnbuf, fqn, flen);
        fqnbuf[flen] = 0;
        body = pm_metal_inspect_src_read(fqnbuf, file + 1);
        if (body == NULL) {
            return -1;
        }
        *ctype = src_ctype(file + 1);
        n = (uint32_t)strlen(body);
        if (n >= out_max) {
            return -1;
        }
        memcpy(out, body, n);
        *out_len = n;
        return 0;
    }
}

static int32_t src_asgi_handler(const char *method, const char *path, uint8_t *out, uint32_t out_max,
    uint32_t *out_len) {
    const char *ctype = NULL;
    if (src_http(method, path, out, out_max, out_len, &ctype) != 0) {
        return -1;
    }
    return 0;
}

/* /build/<fqn> — the build record of the unit's last runtime compile:
 * manifest fields, per-source object sizes, and the linked image's exported
 * symbols. The record exists only after a unit_compile — an unknown fqn or
 * a never-built card is a 404, not an empty record. */
static int32_t build_http(const char *method, const char *path,
    char *out, uint32_t out_max, uint32_t *out_len) {
    const pm_metal_build_record_t *rec;
    const char *p;
    const char *fqn;
    js_t j;
    uint32_t i;
    char fqnbuf[192];
    size_t flen;

    if (method == NULL || strcmp(method, "GET") != 0 || path == NULL) {
        return -1;
    }
    if (strncmp(path, "/build/", 7) != 0) {
        return -1;
    }
    p = path + 7;
    /* a trailing segment is not part of the fqn: /build/<fqn>/<file> serves
     * the authored source of that file (the intermediate panes come from
     * the transpiler cards' own routes once they exist). */
    fqn = p;
    {
        const char *slash = strchr(p, '/');
        if (slash != NULL) {
            flen = (size_t)(slash - fqn);
        } else {
            flen = strlen(fqn);
        }
        if (flen == 0 || flen >= sizeof(fqnbuf)) {
            return -1;
        }
        memcpy(fqnbuf, fqn, flen);
        fqnbuf[flen] = 0;
    }
    rec = pm_metal_build_record_find(fqnbuf);
    if (rec == NULL) {
        return -1;
    }
    j.p = out;
    j.n = 0;
    j.max = out_max;
    js_raw(&j, "{\"fqn\":");
    js_str(&j, rec->fqn);
    js_raw(&j, ",\"n_sources\":");
    js_u32(&j, rec->n_sources);
    js_raw(&j, ",\"objects\":[");
    for (i = 0; i < rec->n_sources; i++) {
        if (i > 0) {
            js_ch(&j, ',');
        }
        js_ch(&j, '{');
        js_raw(&j, "\"src\":");
        js_str(&j, rec->src_paths[i]);
        js_raw(&j, ",\"obj_len\":");
        js_u32(&j, rec->obj_lens[i]);
        js_ch(&j, '}');
    }
    js_raw(&j, "],\"symbols\":[");
    for (i = 0; i < rec->n_syms; i++) {
        if (i > 0) {
            js_ch(&j, ',');
        }
        js_str(&j, rec->sym_names[i]);
    }
    js_raw(&j, "]}");
    if (!js_ok(&j)) {
        return -1;
    }
    *out_len = j.n;
    return 0;
}

static int32_t build_asgi_handler(const char *method, const char *path, uint8_t *out, uint32_t out_max,
    uint32_t *out_len) {
    if (build_http(method, path, (char *)out, out_max, out_len) != 0) {
        return -1;
    }
    return 0;
}

/* ---------------- build face: the webserver build surface ---------------- */

/* The seat's compile fill for on-demand rebuilds — the same includes and
 * defines the host Makefile passes and the build card's own rebuild test
 * derives. Rooted at THIS file (src/pymergetic/metal/inspect), so every
 * seat that can read the tree shares it. The buffers are static: routes
 * must stay valid after the handler returns (unit_compile stores the
 * pointers for the duration of the call only, and the arrays are reused
 * per request, but the fill strings must outlive the compile).
 *
 * POSIX seats only (host binary, unix µPy): firmware and the browser cell
 * refuse the rebuild in the handler — their builds are gated there, and
 * the compiler must not see dead statics. */
#define INSPECT_BUILD_MAX_INC 12u
#define INSPECT_BUILD_MAX_DEF 12u
/* The discover+compile arena: one whole card's TCC objects + ELF image.
 * Same span the build card's rebuild test and ksweep use (64 MiB). */
#define INSPECT_BUILD_SPAN (64u * 1024u * 1024u)

#if !defined(PM_METAL_FIRMWARE) && !defined(PM_METAL_BROWSER)
static char ib_src_root[2560];
static char ib_wasmmod_root[2560];
static char ib_wasmmod_src_root[2560];
static char ib_top_root[2560];
static char ib_tcc_root[2560];
static char ib_libdir_def[2600];
static char ib_triplet_val[160];

static int32_t ib_fill(const char *includes[INSPECT_BUILD_MAX_INC],
    uint32_t *n_inc, const char *defines[INSPECT_BUILD_MAX_DEF],
    uint32_t *n_def) {
    static int ready = 0;
    if (!ready) {
#ifdef PM_METAL_ROOT
        /* the Makefile bakes absolute tree roots (__FILE__ is relative under
         * make, and a rebuild route can run from any CWD) */
        snprintf(ib_src_root, sizeof(ib_src_root), "%s/src", PM_METAL_ROOT);
        snprintf(ib_tcc_root, sizeof(ib_tcc_root), "%s/externals/tcc", PM_METAL_ROOT);
        snprintf(ib_wasmmod_root, sizeof(ib_wasmmod_root), "%s", PM_METAL_WASMMOD_ROOT);
        snprintf(ib_wasmmod_src_root, sizeof(ib_wasmmod_src_root),
            "%s/src", PM_METAL_WASMMOD_ROOT);
        snprintf(ib_top_root, sizeof(ib_top_root), "%s", PM_METAL_TOP_ROOT);
#else
        /* fallback: derive from this file's compiled path (correct only when
         * the CWD is the metal root — the gen'd seats below pass the roots) */
        char dirbuf[2048];
        char *dir;
        snprintf(dirbuf, sizeof(dirbuf), "%s", __FILE__);
        dir = strrchr(dirbuf, '/');
        if (dir == NULL) {
            return -1;
        }
        *dir = '\0';
        dir = strrchr(dirbuf, '/');
        if (dir == NULL) {
            return -1;
        }
        *dir = '\0';
        dir = strrchr(dirbuf, '/');
        if (dir == NULL) {
            return -1;
        }
        *dir = '\0';
        /* dirbuf = <metal>/src/pymergetic */
        snprintf(ib_src_root, sizeof(ib_src_root), "%s/../..", dirbuf);
        snprintf(ib_tcc_root, sizeof(ib_tcc_root), "%s/../../../externals/tcc", dirbuf);
        snprintf(ib_wasmmod_root, sizeof(ib_wasmmod_root), "%s/../../../../wasmmod", dirbuf);
        snprintf(ib_wasmmod_src_root, sizeof(ib_wasmmod_src_root),
            "%s/../../../../wasmmod/src", dirbuf);
        snprintf(ib_top_root, sizeof(ib_top_root), "%s/../../../../..", dirbuf);
#endif
        snprintf(ib_libdir_def, sizeof(ib_libdir_def),
            "PM_METAL_TCC_LIB_DIR=\"%s\"", ib_tcc_root);
        ready = 1;
    }
    *n_inc = 0;
    includes[(*n_inc)++] = ib_src_root;
    includes[(*n_inc)++] = ib_wasmmod_src_root;
    includes[(*n_inc)++] = ib_wasmmod_root;
    includes[(*n_inc)++] = ib_top_root;
    includes[(*n_inc)++] = ib_tcc_root;
    *n_def = 0;
    defines[(*n_def)++] = "PM_WASMMOD_GUEST=0";
    defines[(*n_def)++] = "PM_MOD_TESTS=1";
    defines[(*n_def)++] = "TCC_TARGET_X86_64";
    defines[(*n_def)++] = "PM_HAS_TCC=1";
    defines[(*n_def)++] = ib_libdir_def;
    {
        /* triplet: same probe as the rebuild test — a static value, cached
         * on the first fill (popen is not reentrant in the request path).
         * Firmware has no host cc and no stdio files; the seat's rebuild
         * fill refuses elsewhere, so the probe simply stays empty there. */
        static char triplet[64];
        static int triplet_ready = 0;
#ifndef PM_METAL_FIRMWARE
        if (!triplet_ready) {
            FILE *t = popen("cc -print-multiarch 2>/dev/null", "r");
            if (t != NULL) {
                if (fgets(triplet, sizeof(triplet), t) != NULL) {
                    char *nl = strchr(triplet, '\n');
                    if (nl != NULL) {
                        *nl = '\0';
                    }
                }
                pclose(t);
            }
            triplet_ready = 1;
        }
#endif
        if (triplet[0] != '\0') {
            snprintf(ib_triplet_val, sizeof(ib_triplet_val),
                "CONFIG_TRIPLET=\"%s\"", triplet);
            defines[(*n_def)++] = ib_triplet_val;
        }
    }
    return 0;
}
#endif /* !PM_METAL_FIRMWARE && !PM_METAL_BROWSER */

/* Emit one unit row for the index. */
static void ib_row(js_t *j, const pm_metal_build_unit_t *u) {
    const pm_metal_build_record_t *rec = pm_metal_build_record_find(u->fqn);
    js_raw(j, "{\"fqn\":");
    js_str(j, u->fqn);
    js_raw(j, ",\"impl\":");
    js_str(j, u->impl);
    js_raw(j, ",\"n_sources\":");
    js_u32(j, u->n_sources);
    js_raw(j, ",\"buildable\":");
    js_ch(j, u->n_sources <= PM_METAL_BUILD_MAX_OBJS ? '1' : '0');
    js_raw(j, ",\"built\":");
    js_ch(j, rec != NULL ? '1' : '0');
    js_ch(j, '}');
}

/* GET /build — the tree index: every discovered unit, its impl, and
 * whether the in-kernel chain can build it (c/rs/cpp/py all ride the
 * chain now: rs -> micro-rustc -> C, cpp -> lower -> C, py -> mpy). */
static int32_t build_index_http(const char *method, const char *path,
    char *out, uint32_t out_max, uint32_t *out_len) {
    js_t j;
    pm_metal_build_unit_t *units = NULL;
    uint32_t n_units = 0;
    uint32_t i;
    int32_t st;
    char err[128];
    void *backing;
    pm_util_mem_arena_t *arena;

    if (method == NULL || strcmp(method, "GET") != 0 || path == NULL
        || !path_is(path, "/build")) {
        return -1;
    }
    /* discover only parses manifests — the unit array is small. */
    backing = malloc(1u << 20);
    if (backing == NULL) {
        return -1;
    }
    arena = pm_util_mem_arena_create(backing, 1u << 20);
    if (arena == NULL) {
        free(backing);
        return -1;
    }
    st = pm_metal_build_discover(arena, &units, &n_units, err, sizeof(err));
    if (st != PM_METAL_BUILD_OK) {
        pm_util_mem_arena_destroy(arena);
        free(backing);
        return -1;
    }
    j.p = out;
    j.n = 0;
    j.max = out_max;
    js_raw(&j, "{\"units\":[");
    for (i = 0; i < n_units; i++) {
        if (i > 0) {
            js_ch(&j, ',');
        }
        ib_row(&j, &units[i]);
    }
    js_raw(&j, "]}");
    pm_util_mem_arena_destroy(arena);
    free(backing);
    if (!js_ok(&j)) {
        return -1;
    }
    *out_len = j.n;
    return 0;
}

static int32_t build_index_asgi_handler(const char *method, const char *path,
    uint8_t *out, uint32_t out_max, uint32_t *out_len) {
    if (build_index_http(method, path, (char *)out, out_max, out_len) != 0) {
        return -1;
    }
    return 0;
}

/* POST /build/<fqn> — rebuild that card in-kernel, right now: discover,
 * find the unit, compile its sources with the seat fill (TCC objects),
 * link through the ELF relocator, publish the fresh record. The reply is
 * the same JSON a GET /build/<fqn> serves, plus the build status and any
 * refusal reason. GET on the same path stays the record read.
 *
 * Every seat compiles this handler (the route is the same face); the
 * POSIX rebuild chain only exists where it can run — firmware and the
 * browser cell answer the honest refusal instead. */
static int32_t build_rebuild_http(const char *method, const char *path,
    char *out, uint32_t out_max, uint32_t *out_len) {
    char fqnbuf[192];
    js_t j;

    if (method == NULL || strcmp(method, "POST") != 0 || path == NULL
        || strncmp(path, "/build/", 7) != 0) {
        return -1;
    }
    memcpy(fqnbuf, path + 7, strlen(path + 7) + 1);
    {
        char *q = strchr(fqnbuf, '?');
        if (q != NULL) {
            *q = '\0';
        }
    }
#if defined(PM_METAL_FIRMWARE) || defined(PM_METAL_BROWSER)
    /* Seat fill: firmware has no host cc and no process resolver; the
     * browser cell compiles to wasm32 objects but has no ELF loader and
     * no filesystem to stat the tree. Same posture as jit.py's
     * object_compile refusal, which those proves pin. */
    j.p = out;
    j.n = 0;
    j.max = out_max;
    js_raw(&j, "{\"fqn\":");
    js_str(&j, fqnbuf);
#ifdef PM_METAL_FIRMWARE
    js_raw(&j, ",\"rebuild\":\"refused\",\"error\":\"seat fill: no in-kernel rebuild on firmware\"}");
#else
    js_raw(&j, ",\"rebuild\":\"refused\",\"error\":\"seat fill: no ELF loader in the browser cell\"}");
#endif
    if (!js_ok(&j)) {
        return -1;
    }
    *out_len = j.n;
    return 0;
#else
    {
    const char *includes[INSPECT_BUILD_MAX_INC];
    const char *defines[INSPECT_BUILD_MAX_DEF];
    uint32_t n_inc = 0;
    uint32_t n_def = 0;
    pm_metal_build_unit_t *units = NULL;
    uint32_t n_units = 0;
    uint32_t i;
    const pm_metal_build_unit_t *u = NULL;
    const pm_metal_build_record_t *rec;
    pm_metal_build_artifact_t art;
    char err[PM_METAL_BUILD_ERR_MAX];
    char unit_root[2560];
    size_t flen;
    const char *p;
    int32_t st;
    void *backing;
    pm_util_mem_arena_t *arena;

    p = path + 7;
    flen = strlen(p);
    if (flen == 0 || flen >= sizeof(fqnbuf)) {
        return -1;
    }
    memcpy(fqnbuf, p, flen + 1);
    {
        char *q = strchr(fqnbuf, '?');
        if (q != NULL) {
            *q = '\0';
        }
    }

    backing = malloc(INSPECT_BUILD_SPAN);
    if (backing == NULL) {
        return -1;
    }
    arena = pm_util_mem_arena_create(backing, INSPECT_BUILD_SPAN);
    if (arena == NULL) {
        free(backing);
        return -1;
    }
    st = pm_metal_build_discover(arena, &units, &n_units, err, sizeof(err));
    if (st != PM_METAL_BUILD_OK) {
        goto fail;
    }
    for (i = 0; i < n_units; i++) {
        if (strcmp(units[i].fqn, fqnbuf) == 0) {
            u = &units[i];
            break;
        }
    }
    if (u == NULL) {
        snprintf(err, sizeof(err), "unit not discovered: %.130s", fqnbuf);
        goto fail;
    }
    if (u->n_sources > PM_METAL_BUILD_MAX_OBJS) {
        snprintf(err, sizeof(err), "too many sources: %.130s", fqnbuf);
        goto fail;
    }
    if (strcmp(u->impl, "c") != 0 && strcmp(u->impl, "rs") != 0
        && strcmp(u->impl, "cpp") != 0 && strcmp(u->impl, "py") != 0) {
        snprintf(err, sizeof(err), "unsupported impl=%.130s", u->impl);
        goto fail;
    }
    if (ib_fill(includes, &n_inc, defines, &n_def) != 0) {
        goto fail;
    }
    /* unit_root: the card's real dir — metal cards live under <metal>/src,
     * wasmmod cards (pymergetic.util.*, pymergetic.wasmmod.*) under
     * <wasmmod>/src. The first dir that exists wins; a miss is the refusal
     * errbuf below. */
    {
        const char *roots[2];
        uint32_t r;
        int found = 0;
        roots[0] = ib_src_root;
        roots[1] = ib_wasmmod_src_root;
        for (r = 0; r < 2 && !found; r++) {
            size_t rl = strlen(roots[r]);
            size_t tl = strlen(u->fqn);
            size_t k;
            struct stat st_dir;
            if (rl + tl + 2 > sizeof(unit_root)) {
                continue;
            }
            memcpy(unit_root, roots[r], rl);
            unit_root[rl] = '/';
            memcpy(unit_root + rl + 1, u->fqn, tl);
            unit_root[rl + 1 + tl] = '\0';
            for (k = rl + 1; k < rl + 1 + tl; k++) {
                if (unit_root[k] == '.') {
                    unit_root[k] = '/';
                }
            }
            if (stat(unit_root, &st_dir) == 0 && S_ISDIR(st_dir.st_mode)) {
                found = 1;
            }
        }
        if (!found) {
            snprintf(err, sizeof(err), "unit dir not found: %s", u->fqn);
            goto fail;
        }
    }
    memset(&art, 0, sizeof(art));
    err[0] = '\0';
    st = pm_metal_build_unit_compile(arena, u, unit_root,
        includes, n_inc, defines, n_def, &art, err, sizeof(err));
    if (st != PM_METAL_BUILD_OK) {
        goto fail;
    }
    rec = pm_metal_build_record_find(u->fqn);
    pm_metal_build_artifact_destroy(&art);
    if (rec == NULL) {
        goto fail;
    }
    /* The fresh record, same shape as the GET read. */
    j.p = out;
    j.n = 0;
    j.max = out_max;
    js_raw(&j, "{\"fqn\":");
    js_str(&j, rec->fqn);
    js_raw(&j, ",\"n_sources\":");
    js_u32(&j, rec->n_sources);
    js_raw(&j, ",\"objects\":[");
    for (i = 0; i < rec->n_sources; i++) {
        if (i > 0) {
            js_ch(&j, ',');
        }
        js_ch(&j, '{');
        js_raw(&j, "\"src\":");
        js_str(&j, rec->src_paths[i]);
        js_raw(&j, ",\"obj_len\":");
        js_u32(&j, rec->obj_lens[i]);
        js_ch(&j, '}');
    }
    js_raw(&j, "],\"symbols\":[");
    for (i = 0; i < rec->n_syms; i++) {
        if (i > 0) {
            js_ch(&j, ',');
        }
        js_str(&j, rec->sym_names[i]);
    }
    js_raw(&j, "],\"rebuild\":\"ok\"}");
    pm_util_mem_arena_destroy(arena);
    free(backing);
    if (!js_ok(&j)) {
        return -1;
    }
    *out_len = j.n;
    return 0;

fail:
    pm_util_mem_arena_destroy(arena);
    free(backing);
    /* The refusal is data: 200 with the reason, so a browser build console
     * shows the same text the ksweep report carries. */
    j.p = out;
    j.n = 0;
    j.max = out_max;
    js_raw(&j, "{\"fqn\":");
    js_str(&j, fqnbuf);
    js_raw(&j, ",\"rebuild\":\"refused\",\"error\":");
    js_str(&j, err[0] != '\0' ? err : "unknown unit");
    js_ch(&j, '}');
    if (!js_ok(&j)) {
        return -1;
    }
    *out_len = j.n;
    return 0;
    } /* POSIX rebuild body */
#endif /* PM_METAL_FIRMWARE || PM_METAL_BROWSER */
}

static int32_t build_rebuild_asgi_handler(const char *method, const char *path,
    uint8_t *out, uint32_t out_max, uint32_t *out_len) {
    if (build_rebuild_http(method, path, (char *)out, out_max, out_len) != 0) {
        return -1;
    }
    return 0;
}

/* /docs/<fqn>/<name> — the doc extract for one export face: prose, params,
 * example, provenance (file + line). Unknown fqn/face is 404. */
static int32_t docs_http(const char *method, const char *path,
    char *out, uint32_t out_max, uint32_t *out_len) {
    const char *p;
    const char *slash;
    char fqnbuf[192];
    const char *body;
    size_t flen;
    size_t n;
    if (method == NULL || strcmp(method, "GET") != 0 || path == NULL) {
        return -1;
    }
    if (strncmp(path, "/docs/", 6) != 0) {
        return -1;
    }
    p = path + 6;
    slash = strchr(p, '/');
    if (slash == NULL) {
        return -1;
    }
    flen = (size_t)(slash - p);
    if (flen == 0 || flen >= sizeof(fqnbuf)) {
        return -1;
    }
    memcpy(fqnbuf, p, flen);
    fqnbuf[flen] = 0;
    body = pm_metal_inspect_doc(fqnbuf, slash + 1);
    if (body == NULL) {
        return -1;
    }
    n = strlen(body);
    if (n >= out_max) {
        return -1;
    }
    memcpy(out, body, n);
    *out_len = (uint32_t)n;
    return 0;
}

static int32_t docs_asgi_handler(const char *method, const char *path, uint8_t *out, uint32_t out_max,
    uint32_t *out_len) {
    if (docs_http(method, path, (char *)out, out_max, out_len) != 0) {
        return -1;
    }
    return 0;
}

/* /changes/<target> — the build card's change ledger, read pane: the JSON
 * lines recorded for one target, wrapped as one JSON array. Unknown target
 * is an empty array (a target with no notes is legitimate), a missing
 * build card is 404 (this pane rides on it). */
static int32_t changes_http(const char *method, const char *path,
    char *out, uint32_t out_max, uint32_t *out_len) {
    static char lines[PM_METAL_BUILD_LEDGER_MAX];
    const char *target;
    js_t j;
    int32_t n;
    uint32_t n_lines = 0;
    size_t w;

    if (method == NULL || strcmp(method, "GET") != 0 || path == NULL) {
        return -1;
    }
    if (strncmp(path, "/changes/", 9) != 0) {
        return -1;
    }
    target = path + 9;
    if (target[0] == 0) {
        return -1;
    }
    n = pm_metal_build_notes_query(target, -1, lines, sizeof(lines), &n_lines);
    if (n < 0) {
        return -1;
    }
    j.p = out;
    j.n = 0;
    j.max = out_max;
    js_raw(&j, "{\"target\":");
    js_str(&j, target);
    js_raw(&j, ",\"count\":");
    js_u32(&j, n_lines);
    js_raw(&j, ",\"dbg_n\":");
    js_i32(&j, n);
    js_raw(&j, ",\"lines\":[");
    /* each ledger line is already a JSON object; emit them as array items */
    {
        const char *p = lines;
        const char *end = lines + strlen(lines);
        uint32_t i = 0;
        while (p < end) {
            const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
            size_t lnlen = nl != NULL ? (size_t)(nl - p) : (size_t)(end - p);
            if (lnlen > 0) {
                if (i > 0) {
                    js_ch(&j, ',');
                }
                js_raw(&j, "\"");
                /* the line is JSON — escape the quotes and backslashes only;
                 * the payload is ASCII-safe by construction (note_esc) */
                for (w = 0; w < lnlen; w++) {
                    if (p[w] == '"' || p[w] == '\\') {
                        js_ch(&j, '\\');
                    }
                    js_ch(&j, p[w]);
                }
                js_raw(&j, "\"");
                i++;
            }
            p = nl != NULL ? nl + 1 : end;
        }
    }
    js_raw(&j, "]}");
    if (!js_ok(&j)) {
        return -1;
    }
    *out_len = j.n;
    return 0;
}

static int32_t changes_asgi_handler(const char *method, const char *path, uint8_t *out,
    uint32_t out_max, uint32_t *out_len) {
    if (changes_http(method, path, (char *)out, out_max, out_len) != 0) {
        return -1;
    }
    return 0;
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

/* Every inspect route answers JSON. The paths carry no usable extension (and
 * /inspect/reg/<fqn> ends in a dotted module name), so the type must be stated
 * or the asgi default makes a browser download the reply instead of showing it. */
static int32_t add_route(const char *path) {
    return pm_metal_net_http_asgi_route_fn_ct("GET", path, asgi_handler, "application/json");
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
    /* Card source, directly fetchable on every seat + by the host C prove.
     * Content-type is fixed text because a wildcard route cannot vary it per
     * file; the JS commander gets per-file types from the deferred artifacts
     * pump, which sets its own response type. */
    if (pm_metal_net_http_asgi_route_fn_ct("GET", "/src/*", src_asgi_handler,
            "text/plain; charset=utf-8") != 0) {
        return -1;
    }
    /* Build records: /build/<fqn> serves the provenance of the unit's last
     * runtime compile (objects + linked symbols). */
    if (pm_metal_net_http_asgi_route_fn_ct("GET", "/build/*", build_asgi_handler,
            "application/json") != 0) {
        return -1;
    }
    /* Build face: GET /build is the tree index (every unit, impl, readiness),
     * POST /build/<fqn> rebuilds that card in-kernel and returns the fresh
     * record. Same chain ksweep drives, on demand, over the wire. Firmware
     * and the browser cell wire the same routes; their rebuild fill answers
     * the honest refusal (the handlers exist on every seat). */
    if (pm_metal_net_http_asgi_route_fn_ct("GET", "/build", build_index_asgi_handler,
            "application/json") != 0) {
        return -1;
    }
    if (pm_metal_net_http_asgi_route_fn_ct("POST", "/build/*", build_rebuild_asgi_handler,
            "application/json") != 0) {
        return -1;
    }
    /* Docs: /docs/<fqn>/<fn> serves the extracted comment block of one
     * export face (prose + :param: + :example:). */
    if (pm_metal_net_http_asgi_route_fn_ct("GET", "/docs/*", docs_asgi_handler,
            "application/json") != 0) {
        return -1;
    }
    /* Changes: /changes/<target> serves the ledger lines for one target
     * (all kinds). The build card owns the ledger; this is the read pane. */
    if (pm_metal_net_http_asgi_route_fn_ct("GET", "/changes/*", changes_asgi_handler,
            "application/json") != 0) {
        return -1;
    }
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
PM_MOD_EXPORT_C(pymergetic.metal.inspect, pm_metal_inspect_src_manifest, pm_metal_inspect_src_manifest,
    const char *(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.inspect, pm_metal_inspect_src_read, pm_metal_inspect_src_read,
    const char *(const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.inspect, pm_metal_inspect_doc, pm_metal_inspect_doc,
    const char *(const char *, const char *));
PM_MOD_EXPORT_C(pymergetic.metal.inspect, pm_metal_inspect_example, pm_metal_inspect_example,
    const char *(const char *, const char *));

PM_MOD_BOOT_C(pymergetic.metal.inspect, pm_metal_inspect_init, pm_metal_inspect_deinit);
PM_MOD_BOOTDEP_C(pymergetic.metal.inspect, pymergetic.metal.net.http.asgi);
