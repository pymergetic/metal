/*
 * C into-Py bridge for frozen pymergetic.metal.net.microdot (pm_upy_* bus).
 */
#include "pymergetic/metal/net/microdot/__init__.h"

#include "host.h"
#include "pm_common.h"
#include "pm_upy/obj/attr.h"
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

static uint32_t root_obj(pm_upy_obj_t o)
{
    pm_upy_obj_t none = pm_upy_obj_none();
    int32_t h;

    if (pm_upy_equal(o, none)) {
        return 0;
    }
    h = mp_wasm_handle_register((mp_obj_t)(uintptr_t)o);
    return h > 0 ? (uint32_t)h : 0;
}

static pm_upy_obj_t handle_obj(uint32_t h)
{
    if (h == 0) {
        return pm_upy_obj_none();
    }
    return (pm_upy_obj_t)(uintptr_t)mp_wasm_handle_resolve((int32_t)h);
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

    cls_h = pm_metal_net_microdot_resolve("Microdot");
    if (cls_h == 0) {
        return 0;
    }
    inst = pm_upy_fn_call(cls_h, 0, NULL);
    return root_obj(inst);
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

uint32_t pm_metal_net_microdot_request(void)
{
    return pm_metal_net_microdot_resolve("Request");
}

uint32_t pm_metal_net_microdot_response(void)
{
    return pm_metal_net_microdot_resolve("Response");
}

uint32_t pm_metal_net_microdot_abort(void)
{
    return pm_metal_net_microdot_resolve("abort");
}

uint32_t pm_metal_net_microdot_redirect(void)
{
    return pm_metal_net_microdot_resolve("redirect");
}

uint32_t pm_metal_net_microdot_send_file(void)
{
    return pm_metal_net_microdot_resolve("send_file");
}

uint32_t pm_metal_net_microdot_url_pattern(void)
{
    return pm_metal_net_microdot_resolve("URLPattern");
}

uint32_t pm_metal_net_microdot_async_bytes_io(void)
{
    return pm_metal_net_microdot_resolve("AsyncBytesIO");
}

uint32_t pm_metal_net_microdot_iscoroutine(void)
{
    return pm_metal_net_microdot_resolve("iscoroutine");
}

uint32_t pm_metal_net_microdot_getattr(uint32_t h, const char *attr)
{
    pm_upy_obj_t obj;
    pm_upy_obj_t none;
    pm_upy_obj_t val;

    if (attr == NULL || attr[0] == '\0') {
        return 0;
    }
    obj = handle_obj(h);
    none = pm_upy_obj_none();
    if (pm_upy_equal(obj, none)) {
        return 0;
    }
    val = pm_upy_load_attr(obj, attr);
    return root_obj(val);
}

uint32_t pm_metal_net_microdot_call0(uint32_t h)
{
    pm_upy_obj_t fun;
    pm_upy_obj_t none;
    pm_upy_obj_t ret;

    fun = handle_obj(h);
    none = pm_upy_obj_none();
    if (pm_upy_equal(fun, none)) {
        return 0;
    }
    ret = pm_upy_call_function_0(fun);
    return root_obj(ret);
}

uint32_t pm_metal_net_microdot_call_method0(uint32_t h, const char *method)
{
    pm_upy_obj_t obj;
    pm_upy_obj_t none;
    pm_upy_obj_t ret;

    if (method == NULL || method[0] == '\0') {
        return 0;
    }
    obj = handle_obj(h);
    none = pm_upy_obj_none();
    if (pm_upy_equal(obj, none)) {
        return 0;
    }
    ret = pm_upy_call_method(obj, method, 0, NULL);
    return root_obj(ret);
}

uint32_t pm_metal_net_microdot_call_method1(uint32_t h, const char *method,
                                           const char *arg)
{
    pm_upy_obj_t obj;
    pm_upy_obj_t none;
    pm_upy_obj_t a0;
    pm_upy_obj_t ret;

    if (method == NULL || method[0] == '\0' || arg == NULL) {
        return 0;
    }
    obj = handle_obj(h);
    none = pm_upy_obj_none();
    if (pm_upy_equal(obj, none)) {
        return 0;
    }
    a0 = pm_upy_obj_new_str(arg, strlen(arg));
    ret = pm_upy_call_method(obj, method, 1, &a0);
    return root_obj(ret);
}

uint32_t pm_metal_net_microdot_route(uint32_t app_h)
{
    return pm_metal_net_microdot_getattr(app_h, "route");
}

uint32_t pm_metal_net_microdot_run(uint32_t app_h)
{
    return pm_metal_net_microdot_getattr(app_h, "run");
}

uint32_t pm_metal_net_microdot_get(uint32_t app_h)
{
    return pm_metal_net_microdot_getattr(app_h, "get");
}

uint32_t pm_metal_net_microdot_post(uint32_t app_h)
{
    return pm_metal_net_microdot_getattr(app_h, "post");
}
