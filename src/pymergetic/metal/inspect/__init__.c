#include "pymergetic/metal/inspect/__init__.h"
#include "pymergetic/metal/inspect/py_call.h"
#include "pymergetic/metal/reg/ledger.h"
#include "pymergetic/metal/reg/seats.h"

#include <string.h>

static int g_ready;

static void copy_str(char *dst, size_t dst_len, const char *src)
{
    size_t i = 0;
    if (dst_len == 0) {
        return;
    }
    while (src[i] && i + 1 < dst_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

int32_t pm_metal_inspect_init(void)
{
    g_ready = 1;
    return 0;
}

int32_t pm_metal_inspect_capabilities_json(char *buf, size_t buf_len)
{
    static const char k_caps[] =
        "{\"role\":\"metal\",\"theme\":\"metal\","
        "\"smp\":true,\"asgi\":true,\"ssh_kex\":true,"
        "\"ssh_auth\":true,\"fastapi\":false,"
        "\"microdot\":true,\"vfs_static\":true,"
        "\"static_embed\":false,\"static_backend\":\"wasmmod\"}";

    if (buf == NULL || buf_len < sizeof(k_caps)) {
        return -1;
    }
    copy_str(buf, buf_len, k_caps);
    return (int32_t)(sizeof(k_caps) - 1u);
}

int32_t pm_metal_inspect_handle(const char *method, const char *path,
                                int *status, char *body, size_t body_len)
{
    int32_t py;

    if (!g_ready || method == NULL || path == NULL || status == NULL ||
        body == NULL) {
        return -1;
    }

    /* Prefer frozen MicrodotAdapter; C stubs remain as fallback.
     * py==0 means "not handled / soft-fail" — still try C (query paths often
     * trip MINIMUM-ROM MultiDict AttributeError in Microdot). */
    py = pm_metal_inspect_py_handle(method, path, status, body, body_len);
    if (py > 0) {
        return py;
    }

    if (strcmp(method, "GET") != 0) {
        return 0;
    }
    if (strcmp(path, "/health") == 0) {
        *status = 200;
        copy_str(body, body_len, "{\"ok\":true}");
        return 1;
    }
    if (strcmp(path, "/capabilities") == 0) {
        *status = 200;
        if (pm_metal_inspect_capabilities_json(body, body_len) < 0) {
            *status = 500;
            copy_str(body, body_len, "{\"error\":\"cap\"}");
        }
        return 1;
    }
    if (strcmp(path, "/inspect/self") == 0) {
        static const char k_self[] =
            "{\"schema\":1,\"name\":\"pymergetic.metal\","
            "\"role\":\"kernel\",\"product\":\"metal\",\"org\":\"pymergetic\","
            "\"theme\":\"metal\",\"has_source\":false,\"has_pack\":true,"
            "\"static_backend\":\"wasmmod\","
            "\"source_files\":[],\"pack_files\":[\"httpd.json\"],"
            "\"tags\":{\"role\":\"kernel\",\"product\":\"metal\","
            "\"org\":\"pymergetic\"}}";
        *status = 200;
        copy_str(body, body_len, k_self);
        return 1;
    }
    if (strcmp(path, "/inspect/reg") == 0) {
        int32_t n;
        (void)pm_metal_reg_ledger_seed_pilot();
        n = pm_metal_reg_ledger_json((uint8_t *)body, (uint32_t)body_len);
        if (n < 0 || (size_t)n >= body_len) {
            *status = 500;
            copy_str(body, body_len, "{\"error\":\"reg_ledger\"}");
            return 1;
        }
        /* Append completeness pointers when buffer allows (py adapter prefers). */
        if ((size_t)n + 60u < body_len && n > 0 && body[n - 1] == '}') {
            static const char k_tail[] =
                ",\"completeness_url\":\"/inspect/reg/completeness\","
                "\"completeness_tree_url\":\"/inspect/reg/completeness?fmt=tree\"}";
            body[n - 1] = 0;
            copy_str(body + (n - 1), body_len - (size_t)(n - 1), k_tail);
            n = (int32_t)strlen(body);
        }
        body[n] = 0;
        *status = 200;
        return 1;
    }
    if (strncmp(path, "/inspect/reg/completeness", 25) == 0
        && (path[25] == 0 || path[25] == '?')) {
        const char *q = (path[25] == '?') ? path + 26 : "";
        int32_t gaps_only = 0;
        int32_t detail = 0;
        int32_t fmt_json = 1;
        int32_t n;
        if (strstr(q, "gaps_only=1") != NULL || strstr(q, "gaps_only=true") != NULL) {
            gaps_only = 1;
        }
        if (strstr(q, "detail=1") != NULL || strstr(q, "detail=true") != NULL) {
            detail = 1;
        }
        if (strstr(q, "fmt=tree") != NULL) {
            fmt_json = 0;
        }
        (void)pm_metal_reg_ledger_seed_pilot();
        n = pm_metal_reg_ledger_completeness(NULL, gaps_only, detail, fmt_json, (uint8_t *)body,
                                             (uint32_t)body_len);
        if (n < 0 || (size_t)n >= body_len) {
            *status = 500;
            copy_str(body, body_len, "{\"error\":\"completeness\"}");
            return 1;
        }
        body[n] = 0;
        *status = 200;
        return 1;
    }
    if (strcmp(path, "/inspect/reg/seats") == 0) {
        int32_t n = pm_metal_reg_seats_json(body, (uint32_t)body_len);
        if (n < 0 || (size_t)n >= body_len) {
            *status = 500;
            copy_str(body, body_len, "{\"error\":\"reg_seats\"}");
            return 1;
        }
        body[n] = 0;
        *status = 200;
        return 1;
    }
    if (strncmp(path, "/inspect/reg/", 13) == 0) {
        const char *rest = path + 13;
        const char *slash;
        char mod[128];
        char func[64];
        size_t ml = 0;
        size_t fl = 0;
        int32_t n;

        (void)pm_metal_reg_ledger_seed_pilot();
        slash = strchr(rest, '/');
        if (slash == NULL) {
            /* /inspect/reg/<module> */
            while (*rest && ml + 1 < sizeof(mod)) {
                mod[ml++] = *rest++;
            }
            mod[ml] = 0;
            if (ml == 0) {
                *status = 400;
                copy_str(body, body_len, "{\"error\":\"bad_path\"}");
                return 1;
            }
            n = pm_metal_reg_ledger_module_json((const uint8_t *)mod, (uint8_t *)body,
                                               (uint32_t)body_len);
            if (n < 0 || (size_t)n >= body_len) {
                *status = 404;
                copy_str(body, body_len, "{\"error\":\"not_found\"}");
                return 1;
            }
            body[n] = 0;
            *status = 200;
            return 1;
        }
        while (rest < slash && ml + 1 < sizeof(mod)) {
            mod[ml++] = *rest++;
        }
        mod[ml] = 0;
        rest = slash + 1;
        while (*rest && fl + 1 < sizeof(func)) {
            func[fl++] = *rest++;
        }
        func[fl] = 0;
        if (ml == 0 || fl == 0) {
            *status = 400;
            copy_str(body, body_len, "{\"error\":\"bad_path\"}");
            return 1;
        }
        n = pm_metal_reg_ledger_method_json((const uint8_t *)mod, (const uint8_t *)func,
                                            (uint8_t *)body, (uint32_t)body_len);
        if (n < 0 || (size_t)n >= body_len) {
            *status = 404;
            copy_str(body, body_len, "{\"error\":\"not_found\"}");
            return 1;
        }
        body[n] = 0;
        *status = 200;
        return 1;
    }
    return 0;
}
