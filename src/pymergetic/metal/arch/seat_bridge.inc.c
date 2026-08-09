/*
 * Shared into-Py seat bridge body.
 * Include after defining:
 *   SEAT_MOD     - "pymergetic.metal.arch.<seat>"
 *   SEAT_PREFIX  - pm_metal_arch_<seat>  (token, no quotes)
 *   SEAT_HAS_FIRMWARE - 1 to emit firmware() bridge
 */
#include "pm_common.h"
#include "pm_upy/obj/call.h"
#include "pm_upy/obj/core.h"
#include "pm_upy/obj/module.h"
#include "pm_upy/obj/ops.h"

#include <string.h>

#ifndef SEAT_MOD
#error "SEAT_MOD required"
#endif
#ifndef SEAT_PREFIX
#error "SEAT_PREFIX required"
#endif
#ifndef SEAT_HAS_FIRMWARE
#define SEAT_HAS_FIRMWARE 0
#endif

/* Two-step paste so SEAT_PREFIX expands before ##. */
#define SEAT_FN_PASTE2(prefix, name) prefix##_##name
#define SEAT_FN_PASTE(prefix, name) SEAT_FN_PASTE2(prefix, name)
#define SEAT_FN(name) SEAT_FN_PASTE(SEAT_PREFIX, name)

static void seat_copy_str(char *dst, size_t dst_len, const char *src)
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

static int32_t seat_copy_obj_str(pm_upy_obj_t o, char *buf, size_t buf_len)
{
    const char *s;
    size_t slen;
    pm_upy_obj_t none = pm_upy_obj_none();

    if (pm_upy_equal(o, none)) {
        return -1;
    }
    if (pm_upy_obj_str_get(o, &s, &slen) != PM_OK || s == NULL) {
        return -1;
    }
    seat_copy_str(buf, buf_len, s);
    return (int32_t)strlen(buf);
}

int32_t SEAT_FN(name)(char *buf, size_t buf_len)
{
    pm_upy_obj_t o;

    if (buf == NULL || buf_len == 0) {
        return -1;
    }
    buf[0] = 0;
    o = pm_upy_import_from(SEAT_MOD, "NAME");
    return seat_copy_obj_str(o, buf, buf_len);
}

#if SEAT_HAS_FIRMWARE
int32_t SEAT_FN(firmware)(char *buf, size_t buf_len)
{
    uint32_t fn_h;
    pm_upy_obj_t ret;
    char dotted[96];
    size_t mlen;
    size_t alen;
    static const char k_attr[] = ".firmware";

    if (buf == NULL || buf_len == 0) {
        return -1;
    }
    buf[0] = 0;

    mlen = strlen(SEAT_MOD);
    alen = sizeof(k_attr) - 1u;
    if (mlen + alen + 1u > sizeof(dotted)) {
        return -1;
    }
    memcpy(dotted, SEAT_MOD, mlen);
    memcpy(dotted + mlen, k_attr, alen + 1u);

    fn_h = pm_upy_fn_resolve(dotted);
    if (fn_h != 0) {
        ret = pm_upy_fn_call(fn_h, 0, NULL);
        if (seat_copy_obj_str(ret, buf, buf_len) >= 0) {
            return (int32_t)strlen(buf);
        }
    }

    ret = pm_upy_import_from(SEAT_MOD, "FIRMWARE");
    return seat_copy_obj_str(ret, buf, buf_len);
}
#endif

int32_t SEAT_FN(autoexec)(void)
{
    uint32_t fn_h;
    pm_upy_obj_t ret;
    pm_upy_obj_t none;
    char dotted[96];
    size_t mlen;
    size_t alen;
    static const char k_attr[] = ".autoexec";

    mlen = strlen(SEAT_MOD);
    alen = sizeof(k_attr) - 1u;
    if (mlen + alen + 1u > sizeof(dotted)) {
        return -1;
    }
    memcpy(dotted, SEAT_MOD, mlen);
    memcpy(dotted + mlen, k_attr, alen + 1u);

    fn_h = pm_upy_fn_resolve(dotted);
    if (fn_h == 0) {
        return -1;
    }
    ret = pm_upy_fn_call(fn_h, 0, NULL);
    none = pm_upy_obj_none();
    if (pm_upy_equal(ret, none)) {
        return -1;
    }
    /* False -> fail; True / other non-none -> ok. */
    if (pm_upy_equal(ret, pm_upy_obj_new_bool(0))) {
        return -1;
    }
    return 0;
}

#undef SEAT_FN
