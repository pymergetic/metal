/*
 * pymergetic.metal.process — µPy face over the C process table.
 */
#include "py/obj.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include <string.h>

#include <pymergetic/metal/process/__init__.h>

static mp_obj_t process_current(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_process_current());
}
static MP_DEFINE_CONST_FUN_OBJ_0(process_current_obj, process_current);

static mp_obj_t process_quit(size_t n_args, const mp_obj_t *args)
{
    uint32_t pid = 0;
    int32_t code = 0;
    if (n_args >= 1 && args[0] != mp_const_none) {
        pid = (uint32_t)mp_obj_get_int(args[0]);
    }
    if (n_args >= 2) {
        code = (int32_t)mp_obj_get_int(args[1]);
    }
    return mp_obj_new_int((mp_int_t)pm_metal_process_quit(pid, code));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(process_quit_obj, 0, 2, process_quit);

static mp_obj_t process_crown(size_t n_args, const mp_obj_t *args)
{
    uint32_t h = (uint32_t)mp_obj_get_int(args[0]);
    uint32_t mode = (uint32_t)mp_obj_get_int(args[1]);
    const char *tag = "";
    if (n_args >= 3 && mp_obj_is_str(args[2])) {
        tag = mp_obj_str_get_str(args[2]);
    }
    return mp_obj_new_int((mp_int_t)pm_metal_process_crown(
        h, (pm_metal_process_mode_t)mode, tag, NULL, NULL));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(process_crown_obj, 2, 3, process_crown);

static mp_obj_t process_list(void)
{
    pm_metal_process_info_t infos[PM_METAL_PROCESS_MAX];
    uint32_t n = pm_metal_process_list(infos, PM_METAL_PROCESS_MAX);
    mp_obj_t list = mp_obj_new_list(0, NULL);
    uint32_t i;
    for (i = 0; i < n; i++) {
        mp_obj_t items[] = {
            mp_obj_new_int((mp_int_t)infos[i].pid),
            mp_obj_new_int((mp_int_t)infos[i].async_handle),
            mp_obj_new_int((mp_int_t)infos[i].mode),
            mp_obj_new_str(infos[i].tag, strlen(infos[i].tag)),
        };
        /* (pid, handle, mode, tag) */
        mp_obj_list_append(list, mp_obj_new_tuple(4, items));
    }
    return list;
}
static MP_DEFINE_CONST_FUN_OBJ_0(process_list_obj, process_list);

static mp_obj_t process_shutting_down(void)
{
    return mp_obj_new_bool(pm_metal_process_shutting_down() != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(process_shutting_down_obj, process_shutting_down);

static const mp_rom_map_elem_t process_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_process) },
    { MP_ROM_QSTR(MP_QSTR_current), MP_ROM_PTR(&process_current_obj) },
    { MP_ROM_QSTR(MP_QSTR_quit), MP_ROM_PTR(&process_quit_obj) },
    { MP_ROM_QSTR(MP_QSTR_crown), MP_ROM_PTR(&process_crown_obj) },
    { MP_ROM_QSTR(MP_QSTR_list), MP_ROM_PTR(&process_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_ps), MP_ROM_PTR(&process_list_obj) },
    { MP_ROM_QSTR(MP_QSTR_shutting_down), MP_ROM_PTR(&process_shutting_down_obj) },
    { MP_ROM_QSTR(MP_QSTR_FG), MP_ROM_INT(PM_METAL_PROCESS_MODE_FG) },
    { MP_ROM_QSTR(MP_QSTR_BG), MP_ROM_INT(PM_METAL_PROCESS_MODE_BG) },
    { MP_ROM_QSTR(MP_QSTR_DAEMON), MP_ROM_INT(PM_METAL_PROCESS_MODE_DAEMON) },
};
static MP_DEFINE_CONST_DICT(process_globals, process_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_process = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&process_globals,
};

int32_t pm_metal_process_seat_test(void) __attribute__((weak));
