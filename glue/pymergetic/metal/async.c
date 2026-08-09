/*
 * pymergetic.metal.async — µPy face.
 * Firmware seats only.
 */
#include "py/obj.h"
#include "py/runtime.h"

#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/runner.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/async/await.h>
#include <pymergetic/metal/async/prio.h>
#include <pymergetic/metal/async/smp.h>
#include <pymergetic/metal/reg/seats.h>

static mp_obj_t async_status(mp_obj_t h_obj)
{
    mp_int_t h = mp_obj_get_int(h_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_async_status((uint32_t)h));
}
static MP_DEFINE_CONST_FUN_OBJ_1(async_status_obj, async_status);

static mp_obj_t async_set_result_u32(mp_obj_t h_obj, mp_obj_t v_obj)
{
    mp_int_t h = mp_obj_get_int(h_obj);
    mp_int_t v = mp_obj_get_int(v_obj);
    pm_metal_async_set_result_u32((uint32_t)h, (uint32_t)v);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(async_set_result_u32_obj, async_set_result_u32);

static mp_obj_t async_result_u32(mp_obj_t h_obj)
{
    mp_int_t h = mp_obj_get_int(h_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_async_result_u32((uint32_t)h));
}
static MP_DEFINE_CONST_FUN_OBJ_1(async_result_u32_obj, async_result_u32);

static mp_obj_t async_completed_u32(mp_obj_t v_obj)
{
    mp_int_t v = mp_obj_get_int(v_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_async_completed_u32((uint32_t)v));
}
static MP_DEFINE_CONST_FUN_OBJ_1(async_completed_u32_obj, async_completed_u32);

static mp_obj_t async_coro_close(mp_obj_t h_obj)
{
    mp_int_t h = mp_obj_get_int(h_obj);
    pm_metal_async_coro_close((uint32_t)h);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(async_coro_close_obj, async_coro_close);

static mp_obj_t async_start(mp_obj_t n_cpus_obj)
{
    mp_int_t n_cpus = mp_obj_get_int(n_cpus_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_async_start((uint32_t)n_cpus));
}
static MP_DEFINE_CONST_FUN_OBJ_1(async_start_obj, async_start);

static mp_obj_t async_ready(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_async_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(async_ready_obj, async_ready);

static mp_obj_t async_n_runners(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_async_n_runners());
}
static MP_DEFINE_CONST_FUN_OBJ_0(async_n_runners_obj, async_n_runners);

static mp_obj_t async_create_task(mp_obj_t h_obj)
{
    mp_int_t h = mp_obj_get_int(h_obj);
    return mp_obj_new_int((mp_int_t)pm_metal_async_create_task((uint32_t)h));
}
static MP_DEFINE_CONST_FUN_OBJ_1(async_create_task_obj, async_create_task);

static mp_obj_t async_run_poll(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_async_run_poll());
}
static MP_DEFINE_CONST_FUN_OBJ_0(async_run_poll_obj, async_run_poll);

static mp_obj_t async_run_poll_all(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_async_run_poll_all());
}
static MP_DEFINE_CONST_FUN_OBJ_0(async_run_poll_all_obj, async_run_poll_all);

static mp_obj_t async_run_loop(void)
{
    return mp_obj_new_int((mp_int_t)pm_metal_async_run_loop());
}
static MP_DEFINE_CONST_FUN_OBJ_0(async_run_loop_obj, async_run_loop);

static const mp_rom_map_elem_t async_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal_dot_async) },
    { MP_ROM_QSTR(MP_QSTR_status), MP_ROM_PTR(&async_status_obj) },
    { MP_ROM_QSTR(MP_QSTR_set_result_u32), MP_ROM_PTR(&async_set_result_u32_obj) },
    { MP_ROM_QSTR(MP_QSTR_result_u32), MP_ROM_PTR(&async_result_u32_obj) },
    { MP_ROM_QSTR(MP_QSTR_completed_u32), MP_ROM_PTR(&async_completed_u32_obj) },
    { MP_ROM_QSTR(MP_QSTR_coro_close), MP_ROM_PTR(&async_coro_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_start), MP_ROM_PTR(&async_start_obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&async_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_n_runners), MP_ROM_PTR(&async_n_runners_obj) },
    { MP_ROM_QSTR(MP_QSTR_create_task), MP_ROM_PTR(&async_create_task_obj) },
    { MP_ROM_QSTR(MP_QSTR_run_poll), MP_ROM_PTR(&async_run_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_run_poll_all), MP_ROM_PTR(&async_run_poll_all_obj) },
    { MP_ROM_QSTR(MP_QSTR_run_loop), MP_ROM_PTR(&async_run_loop_obj) },
};
static MP_DEFINE_CONST_DICT(async_globals, async_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal_async = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&async_globals,
};

PM_METAL_REG_SEAT(g_pm_seat_async, "pymergetic.metal.async", PM_METAL_REG_SEAT_GLUE, 1, 1, NULL);
