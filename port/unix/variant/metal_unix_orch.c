/*
 * Unix seat orchestration bind — process + boot faces onto frozen metal.
 * Async/mem/unboot are real linked muscle (see mpconfigvariant.mk).
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <stdlib.h>

#include <pymergetic/metal/async/runner.h>
#include <pymergetic/metal/boot/unboot.h>
#include <pymergetic/metal/mem.h>
#include <pymergetic/metal/process/__init__.h>

#ifndef PM_METAL_UNIX_HEAP_BYTES
#define PM_METAL_UNIX_HEAP_BYTES (4u * 1024u * 1024u)
#endif

/* Keep these literals: makeqstr turns "a.b.c" into MP_QSTR_a_dot_b_dot_c
 * with content "a.b.c" (not the `_dot_` text). File is on SRC_QSTR. */
static const char *const pm_unix_modname_seeds[] = {
    "pymergetic.metal",
    "pymergetic.metal.process",
    "pymergetic.metal.boot",
    "pymergetic.metal.async",
};
enum { pm_unix_modname_seeds_n = sizeof(pm_unix_modname_seeds) / sizeof(pm_unix_modname_seeds[0]) };

static uint8_t g_unix_heap[PM_METAL_UNIX_HEAP_BYTES] __attribute__((aligned(4096)));
static int g_floor_ready;

static void unix_shutdown_hook(void)
{
    exit(0);
}

static void unix_reboot_hook(void)
{
    exit(0);
}

static void unix_floor_start(void)
{
    if (g_floor_ready) {
        return;
    }
    if (pm_metal_mem_init(g_unix_heap, sizeof(g_unix_heap)) != 0) {
        return;
    }
    if (pm_metal_async_start(1) != 0) {
        return;
    }
    g_floor_ready = 1;
}

static mp_obj_t boot_unboot(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_boot_unboot());
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_unboot_obj, boot_unboot);

static mp_obj_t boot_shutting_down(void)
{
    return mp_obj_new_bool(pm_metal_boot_shutting_down() != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_shutting_down_obj, boot_shutting_down);

static mp_obj_t boot_shutdown(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_boot_shutdown());
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_shutdown_obj, boot_shutdown);

static mp_obj_t boot_reboot(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_boot_reboot());
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_reboot_obj, boot_reboot);

static mp_obj_t boot_is_dead(void)
{
    return mp_obj_new_bool(pm_metal_boot_is_dead() != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_0(boot_is_dead_obj, boot_is_dead);

static const mp_rom_map_elem_t boot_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_boot) },
    { MP_ROM_QSTR(MP_QSTR_unboot), MP_ROM_PTR(&boot_unboot_obj) },
    { MP_ROM_QSTR(MP_QSTR_shutting_down), MP_ROM_PTR(&boot_shutting_down_obj) },
    { MP_ROM_QSTR(MP_QSTR_shutdown), MP_ROM_PTR(&boot_shutdown_obj) },
    { MP_ROM_QSTR(MP_QSTR_reboot), MP_ROM_PTR(&boot_reboot_obj) },
    { MP_ROM_QSTR(MP_QSTR_is_dead), MP_ROM_PTR(&boot_is_dead_obj) },
};
static MP_DEFINE_CONST_DICT(boot_globals, boot_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_boot_unix = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&boot_globals,
};

MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_boot, mp_module_pymergetic_metal_boot_unix);

extern const mp_obj_module_t mp_module_pymergetic_metal_process;
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal_dot_process, mp_module_pymergetic_metal_process);

static mp_obj_t metal_unix_orch_bind(void)
{
    mp_obj_t metal = mp_import_name(MP_QSTR_pymergetic_dot_metal, mp_const_none, MP_OBJ_NEW_SMALL_INT(0));
    mp_obj_t process = MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_process);
    mp_obj_t boot = MP_OBJ_FROM_PTR(&mp_module_pymergetic_metal_boot_unix);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)),
                      MP_OBJ_NEW_QSTR(MP_QSTR_pymergetic_dot_metal_dot_process), process);
    mp_obj_dict_store(MP_OBJ_FROM_PTR(&MP_STATE_VM(mp_loaded_modules_dict)),
                      MP_OBJ_NEW_QSTR(MP_QSTR_pymergetic_dot_metal_dot_boot), boot);
    (void)metal;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_unix_orch_bind_obj, metal_unix_orch_bind);

static const mp_rom_map_elem_t orch_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_metal_unix_orch) },
    { MP_ROM_QSTR(MP_QSTR_bind), MP_ROM_PTR(&metal_unix_orch_bind_obj) },
};
static MP_DEFINE_CONST_DICT(orch_globals, orch_globals_table);

const mp_obj_module_t mp_module_metal_unix_orch = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&orch_globals,
};
MP_REGISTER_MODULE(MP_QSTR_metal_unix_orch, mp_module_metal_unix_orch);

void pm_metal_unix_orch_init(void)
{
    (void)pm_unix_modname_seeds_n;
    (void)pm_unix_modname_seeds[0];
    unix_floor_start();
    pm_metal_boot_set_shutdown_hook(unix_shutdown_hook);
    pm_metal_boot_set_reboot_hook(unix_reboot_hook);
}
