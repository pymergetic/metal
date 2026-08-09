/*
 * pymergetic.metal.fs.embed — µPy face.
 */
#include "py/obj.h"
#include "py/runtime.h"
#include <pymergetic/metal/fs/embed/__init__.h>
#include <string.h>
#include <pymergetic/metal/reg/seats.h>


static mp_obj_t embed_emit(mp_obj_t name_obj, mp_obj_t data_obj, int is_rs)
{
    size_t nlen;
    const char *name = mp_obj_str_get_data(name_obj, &nlen);
    uint8_t name_z[129];
    mp_buffer_info_t data;
    vstr_t vstr;
    size_t out_len = 0;
    int32_t rc;

    if (nlen == 0u || nlen >= sizeof name_z) {
        mp_raise_ValueError(MP_ERROR_TEXT("embed name"));
    }
    memcpy(name_z, name, nlen);
    name_z[nlen] = 0;
    mp_get_buffer_raise(data_obj, &data, MP_BUFFER_READ);
    vstr_init_len(&vstr, data.len * 6u + 256u);
    if (is_rs) {
        rc = pm_metal_fs_embed_rs(name_z, (const uint8_t *)data.buf, data.len, (uint8_t *)vstr.buf,
                                  vstr.len, &out_len);
    } else {
        rc = pm_metal_fs_embed_c(name_z, (const uint8_t *)data.buf, data.len, (uint8_t *)vstr.buf,
                                 vstr.len, &out_len);
    }
    if (rc != 0) {
        vstr_clear(&vstr);
        mp_raise_ValueError(MP_ERROR_TEXT("embed"));
    }
    {
        mp_obj_t out = mp_obj_new_str(vstr.buf, out_len);
        vstr_clear(&vstr);
        return out;
    }
}

static mp_obj_t embed_c(mp_obj_t name_obj, mp_obj_t data_obj)
{
    return embed_emit(name_obj, data_obj, 0);
}
static MP_DEFINE_CONST_FUN_OBJ_2(embed_c_obj, embed_c);

static mp_obj_t embed_rs(mp_obj_t name_obj, mp_obj_t data_obj)
{
    return embed_emit(name_obj, data_obj, 1);
}
static MP_DEFINE_CONST_FUN_OBJ_2(embed_rs_obj, embed_rs);

static const mp_rom_map_elem_t embed_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_fs_dot_embed) },
    { MP_ROM_QSTR(MP_QSTR_embed_c), MP_ROM_PTR(&embed_c_obj) },
    { MP_ROM_QSTR(MP_QSTR_embed_rs), MP_ROM_PTR(&embed_rs_obj) },
};
static MP_DEFINE_CONST_DICT(embed_globals, embed_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_fs_embed = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&embed_globals,
};


PM_METAL_REG_SEAT(g_pm_seat_fs_embed, "pymergetic.metal.fs.embed", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
