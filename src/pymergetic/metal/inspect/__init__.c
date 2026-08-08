#include "pymergetic/metal/inspect/__init__.h"

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
        "\"ssh_auth\":false,\"fastapi\":false,"
        "\"microdot\":true,\"vfs_static\":true}";

    if (buf == NULL || buf_len < sizeof(k_caps)) {
        return -1;
    }
    copy_str(buf, buf_len, k_caps);
    return (int32_t)(sizeof(k_caps) - 1u);
}

int32_t pm_metal_inspect_handle(const char *method, const char *path,
                                int *status, char *body, size_t body_len)
{
    if (!g_ready || method == NULL || path == NULL || status == NULL ||
        body == NULL) {
        return -1;
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
        *status = 501;
        copy_str(body, body_len,
                 "{\"error\":\"NotImplemented\",\"path\":\"/inspect/self\"}");
        return 1;
    }
    return 0;
}
