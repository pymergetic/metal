/*
 * Into-Py bridges for frozen pymergetic.metal.arch (seat packs stay in arch.c).
 */
#include "pymergetic/metal/arch.h"

#include "pm_common.h"
#include "pm_upy/obj/call.h"
#include "pm_upy/obj/core.h"
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

int32_t pm_metal_arch_py_name(char *buf, size_t buf_len)
{
    uint32_t fn_h;
    pm_upy_obj_t ret;
    pm_upy_obj_t none;
    const char *s;
    size_t slen;

    if (buf == NULL || buf_len == 0) {
        return -1;
    }
    buf[0] = 0;

    fn_h = pm_upy_fn_resolve("pymergetic.metal.arch.name");
    if (fn_h == 0) {
        return -1;
    }
    ret = pm_upy_fn_call(fn_h, 0, NULL);
    none = pm_upy_obj_none();
    if (pm_upy_equal(ret, none)) {
        return -1;
    }
    if (pm_upy_obj_str_get(ret, &s, &slen) != PM_OK || s == NULL) {
        return -1;
    }
    copy_str(buf, buf_len, s);
    return (int32_t)strlen(buf);
}

int32_t pm_metal_arch_py_names(char *buf, size_t buf_len)
{
    uint32_t fn_h;
    pm_upy_obj_t ret;
    pm_upy_obj_t none;
    size_t n;
    size_t i;
    size_t pos;

    if (buf == NULL || buf_len == 0) {
        return -1;
    }
    buf[0] = 0;

    fn_h = pm_upy_fn_resolve("pymergetic.metal.arch.names");
    if (fn_h == 0) {
        return -1;
    }
    ret = pm_upy_fn_call(fn_h, 0, NULL);
    none = pm_upy_obj_none();
    if (pm_upy_equal(ret, none)) {
        return -1;
    }

    n = pm_upy_len(ret);
    pos = 0;
    for (i = 0; i < n; i++) {
        pm_upy_obj_t idx = pm_upy_obj_new_int((intptr_t)i);
        pm_upy_obj_t item = pm_upy_call_method(ret, "__getitem__", 1, &idx);
        const char *s;
        size_t slen;
        size_t j;

        if (pm_upy_equal(item, none)) {
            return -1;
        }
        if (pm_upy_obj_str_get(item, &s, &slen) != PM_OK || s == NULL) {
            return -1;
        }
        if (i > 0) {
            if (pos + 1u >= buf_len) {
                return -1;
            }
            buf[pos++] = ',';
        }
        for (j = 0; j < slen; j++) {
            if (pos + 1u >= buf_len) {
                return -1;
            }
            buf[pos++] = s[j];
        }
    }
    buf[pos] = 0;
    return (int32_t)pos;
}
