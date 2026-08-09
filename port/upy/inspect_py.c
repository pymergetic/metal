/*
 * C → frozen Microdot Inspect dispatch (live-http JSON routes).
 */
#include <string.h>

#include "py/obj.h"
#include "py/objstr.h"
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
    nlr_buf_t nlr;
    mp_obj_t mod;
    mp_obj_t fun;
    mp_obj_t ret;
    size_t n;
    mp_obj_t *items;
    const char *s;

    if (method == NULL || path == NULL || status == NULL || body == NULL) {
        return -1;
    }

    if (nlr_push(&nlr) != 0) {
        return -1;
    }

    /* Non-empty fromlist so import returns the leaf module, not pymergetic. */
    mod = mp_import_name(qstr_from_str("pymergetic.metal.inspect.dispatch"),
                         mp_obj_new_str("*", 1), MP_OBJ_NEW_SMALL_INT(0));
    fun = mp_load_attr(mod, qstr_from_str("handle"));
    ret = mp_call_function_2(fun, mp_obj_new_str(method, strlen(method)),
                             mp_obj_new_str(path, strlen(path)));

    if (ret == mp_const_none) {
        nlr_pop();
        return 0;
    }

    mp_obj_get_array(ret, &n, &items);
    if (n != 2u) {
        nlr_pop();
        return -1;
    }
    *status = (int)mp_obj_get_int(items[0]);
    s = mp_obj_str_get_str(items[1]);
    copy_str(body, body_len, s);
    nlr_pop();
    return 1;
}
