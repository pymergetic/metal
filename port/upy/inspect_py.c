/*
 * C → frozen Microdot Inspect dispatch via wasmmod pm_upy_* bus.
 */
#include <string.h>

#include "host.h"
#include "pm_common.h"
#include "pm_upy/obj/call.h"
#include "pm_upy/obj/core.h"
#include "pm_upy/obj/ops.h"

#include "py/nlr.h"
#include "py/obj.h"
#include "py/runtime.h"

#include "pymergetic/metal/inspect/py_call.h"

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

int32_t pm_metal_inspect_py_handle(const char *method, const char *path,
                                   int *status, char *body, size_t body_len)
{
    uint32_t fn_h;
    pm_upy_obj_t args[2];
    pm_upy_obj_t ret;
    pm_upy_obj_t none;
    mp_obj_t status_o;
    mp_obj_t body_o;
    const char *s;
    size_t slen;
    nlr_buf_t nlr;
    int32_t rc = -1;

    if (method == NULL || path == NULL || status == NULL || body == NULL) {
        return -1;
    }

    fn_h = pm_upy_fn_resolve("pymergetic.metal.inspect.dispatch.handle");
    if (fn_h == 0) {
        return -1;
    }

    args[0] = pm_upy_obj_new_str(method, strlen(method));
    args[1] = pm_upy_obj_new_str(path, strlen(path));
    ret = pm_upy_fn_call(fn_h, 2, args);

    none = pm_upy_obj_none();
    if (pm_upy_equal(ret, none)) {
        /* Not an Inspect route (None) or call soft-fail — both are none. */
        return 0;
    }

    /* Subscript — never mp_load_attr("__getitem__") (MINIMUM ROM AttributeError). */
    if (nlr_push(&nlr) == 0) {
        mp_obj_t tup = (mp_obj_t)(uintptr_t)ret;
        if (mp_obj_get_int(mp_obj_len(tup)) != 2) {
            nlr_pop();
            return -1;
        }
        status_o = mp_obj_subscr(tup, MP_OBJ_NEW_SMALL_INT(0), MP_OBJ_SENTINEL);
        body_o = mp_obj_subscr(tup, MP_OBJ_NEW_SMALL_INT(1), MP_OBJ_SENTINEL);
        *status = (int)mp_obj_get_int(status_o);
        s = mp_obj_str_get_data(body_o, &slen);
        if (s == NULL) {
            nlr_pop();
            return -1;
        }
        copy_str(body, body_len, s);
        rc = 1;
        nlr_pop();
    } else {
        rc = -1;
    }
    return rc;
}

uint32_t pm_metal_inspect_py_create_app(void)
{
    uint32_t fn_h;
    pm_upy_obj_t inst;
    pm_upy_obj_t none;
    int32_t h;

    fn_h = pm_upy_fn_resolve("pymergetic.metal.inspect.create_app");
    if (fn_h == 0) {
        return 0;
    }
    inst = pm_upy_fn_call(fn_h, 0, NULL);
    none = pm_upy_obj_none();
    if (pm_upy_equal(inst, none)) {
        return 0;
    }
    h = mp_wasm_handle_register((mp_obj_t)(uintptr_t)inst);
    return h > 0 ? (uint32_t)h : 0;
}

void pm_metal_inspect_py_close(uint32_t h)
{
    if (h == 0) {
        return;
    }
    (void)mp_wasm_handle_free((int32_t)h);
}
