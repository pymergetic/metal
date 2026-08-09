/*
 * C into-Py bridge for frozen pymergetic.metal.net.microdot (pm_upy_* bus).
 */
#include "pymergetic/metal/net/microdot/__init__.h"

#include "host.h"
#include "pm_common.h"
#include "pm_upy/obj/call.h"
#include "pm_upy/obj/core.h"
#include "pm_upy/obj/module.h"
#include "pm_upy/obj/ops.h"

#include <string.h>

static void copy_str(char *dst, size_t dst_len, const char *src)
{
    size_t i = 0;
    if (dst_len == 0) {
        return;
    }
    while (src[i] && i + 1u < dst_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int build_dotted(char *dst, size_t dst_len, const char *attr)
{
    static const char k_prefix[] = "pymergetic.metal.net.microdot.";
    size_t plen = sizeof(k_prefix) - 1u;
    size_t alen;

    if (attr == NULL || attr[0] == '\0') {
        return -1;
    }
    alen = strlen(attr);
    if (plen + alen + 1u > dst_len) {
        return -1;
    }
    memcpy(dst, k_prefix, plen);
    memcpy(dst + plen, attr, alen);
    dst[plen + alen] = 0;
    return 0;
}

uint32_t pm_metal_net_microdot_resolve(const char *attr)
{
    char dotted[192];

    if (build_dotted(dotted, sizeof(dotted), attr) != 0) {
        return 0;
    }
    return pm_upy_fn_resolve(dotted);
}

uint32_t pm_metal_net_microdot_new(void)
{
    uint32_t cls_h;
    pm_upy_obj_t inst;
    pm_upy_obj_t none;
    int32_t h;

    cls_h = pm_metal_net_microdot_resolve("Microdot");
    if (cls_h == 0) {
        return 0;
    }
    inst = pm_upy_fn_call(cls_h, 0, NULL);
    none = pm_upy_obj_none();
    if (pm_upy_equal(inst, none)) {
        return 0;
    }
    /* Instance handle (fn_resolve only roots callables). */
    h = mp_wasm_handle_register((mp_obj_t)(uintptr_t)inst);
    return h > 0 ? (uint32_t)h : 0;
}

void pm_metal_net_microdot_close(uint32_t h)
{
    if (h == 0) {
        return;
    }
    (void)mp_wasm_handle_free((int32_t)h);
}

int32_t pm_metal_net_microdot_version(char *buf, size_t buf_len)
{
    pm_upy_obj_t ver;
    pm_upy_obj_t none;
    const char *s;
    size_t slen;

    if (buf == NULL || buf_len == 0) {
        return -1;
    }
    buf[0] = 0;

    ver = pm_upy_import_from("pymergetic.metal.net.microdot", "__version__");
    none = pm_upy_obj_none();
    if (pm_upy_equal(ver, none)) {
        return -1;
    }
    if (pm_upy_obj_str_get(ver, &s, &slen) != PM_OK || s == NULL) {
        return -1;
    }
    copy_str(buf, buf_len, s);
    return (int32_t)strlen(buf);
}
