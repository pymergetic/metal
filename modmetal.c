/*
 * Builtin pymergetic.metal package shell.
 * Heap is pymergetic.util.mem (impl=c) — C/RS include that face; no
 * pymergetic.metal.mem twin. Real Metal leaves (async, net, …) land here
 * with the same one-lang + faces rule.
 */
#include "extmod/metal/modmetal.h"

#include "extmod/metal/boot.h"
#include "ports/micropython/finder.h"
#include "ports/micropython/importhook.h"
#include "py/nlr.h"
#include "py/obj.h"
#include "py/objtuple.h"
#include "py/runtime.h"
#include "pymergetic/metal/async.h"
#include "pymergetic/metal/boot/externals.h"
#include "pymergetic/metal/boot/tree.h"
#include "pymergetic/metal/drivers/__types__.h"
#include "pymergetic/wasmmod/boot.h"
#include "pymergetic/wasmmod/net/cdn.h"

#include <string.h>

#ifndef PM_METAL_UPY_GEN_N
#define PM_METAL_UPY_GEN_N 8
#endif

#ifndef PM_METAL_DRV_PY_MAX
#define PM_METAL_DRV_PY_MAX 4
#endif

MP_REGISTER_ROOT_POINTER(mp_obj_t metal_upy_gen[PM_METAL_UPY_GEN_N]);
MP_REGISTER_ROOT_POINTER(mp_obj_t metal_drv_py_attach[PM_METAL_DRV_PY_MAX]);

typedef struct {
    pm_metal_async_coro_t coro;
    uint32_t slot;
} pm_metal_upy_frame_t;

static void metal_ensure(void) {
    mp_wasm_ensure_inited();
    if (!pm_metal_ready() && pm_metal_boot() != 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("metal boot failed"));
    }
}

static mp_obj_t metal___init__(void) {
    metal_ensure();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal___init___obj, metal___init__);

static mp_obj_t metal_ready(void) {
    metal_ensure();
    return mp_obj_new_bool(pm_metal_ready());
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_ready_obj, metal_ready);

static mp_obj_t metal_poll(void) {
    metal_ensure();
    pm_metal_async_poll();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(metal_poll_obj, metal_poll);

static pm_metal_async_status_t step_upy(pm_metal_async_coro_t *self) {
    pm_metal_upy_frame_t *f = (pm_metal_upy_frame_t *)self;
    mp_obj_t gen;
    nlr_buf_t nlr;
    if (f->slot >= PM_METAL_UPY_GEN_N) {
        return PM_METAL_ASYNC_ERROR;
    }
    gen = MP_STATE_VM(metal_upy_gen)[f->slot];
    if (gen == MP_OBJ_NULL) {
        return PM_METAL_ASYNC_ERROR;
    }
    if (nlr_push(&nlr) == 0) {
        mp_obj_t v = mp_iternext(gen);
        nlr_pop();
        if (v == MP_OBJ_STOP_ITERATION) {
            MP_STATE_VM(metal_upy_gen)[f->slot] = MP_OBJ_NULL;
            return PM_METAL_ASYNC_DONE;
        }
        return pm_metal_async_yield_park(self);
    }
    MP_STATE_VM(metal_upy_gen)[f->slot] = MP_OBJ_NULL;
    return PM_METAL_ASYNC_ERROR;
}

static mp_obj_t metal_register_upy(mp_obj_t gen) {
    uint32_t i;
    pm_metal_upy_frame_t *frame;
    metal_ensure();
    if (!pm_metal_ready()) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("metal not ready"));
    }
    for (i = 0; i < PM_METAL_UPY_GEN_N; i++) {
        if (MP_STATE_VM(metal_upy_gen)[i] == MP_OBJ_NULL) {
            break;
        }
    }
    if (i >= PM_METAL_UPY_GEN_N) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("upy gen slots full"));
    }
    frame = (pm_metal_upy_frame_t *)pm_metal_async_coro_create(step_upy, sizeof(*frame));
    if (frame == NULL) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("upy coro"));
    }
    frame->slot = i;
    MP_STATE_VM(metal_upy_gen)[i] = gen;
    if (pm_metal_async_create_task(&frame->coro) == NULL) {
        MP_STATE_VM(metal_upy_gen)[i] = MP_OBJ_NULL;
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("upy task"));
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(metal_register_upy_obj, metal_register_upy);

#define PM_METAL_DRV_PY_DEF(i) \
    static int32_t metal_drv_py_attach_##i(int32_t bus, uint32_t loc0, uint32_t loc1, uint32_t loc2, \
        uint32_t loc3) { \
        mp_obj_t cb = MP_STATE_VM(metal_drv_py_attach)[i]; \
        nlr_buf_t nlr; \
        if (cb == MP_OBJ_NULL) { \
            return -1; \
        } \
        if (nlr_push(&nlr) == 0) { \
            mp_obj_t args[5] = { \
                mp_obj_new_int(bus), \
                mp_obj_new_int_from_uint(loc0), \
                mp_obj_new_int_from_uint(loc1), \
                mp_obj_new_int_from_uint(loc2), \
                mp_obj_new_int_from_uint(loc3), \
            }; \
            mp_obj_t res = mp_call_function_n_kw(cb, 5, 0, args); \
            int32_t out = (int32_t)mp_obj_get_int(res); \
            nlr_pop(); \
            return out; \
        } \
        return -1; \
    }
PM_METAL_DRV_PY_DEF(0)
PM_METAL_DRV_PY_DEF(1)
PM_METAL_DRV_PY_DEF(2)
PM_METAL_DRV_PY_DEF(3)

static pm_metal_drv_attach_fn s_drv_attach[PM_METAL_DRV_PY_MAX] = {
    metal_drv_py_attach_0,
    metal_drv_py_attach_1,
    metal_drv_py_attach_2,
    metal_drv_py_attach_3,
};
static pm_metal_drv_t s_drv_rec[PM_METAL_DRV_PY_MAX];
static char s_drv_mod[PM_METAL_DRV_PY_MAX][80];
static uint32_t s_drv_n;

static void copy_mod(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (src == NULL || cap == 0) {
        return;
    }
    while (src[n] != 0 && n + 1u < cap) {
        dst[n] = src[n];
        n++;
    }
    dst[n] = 0;
}

static int32_t drv_bind(const char *mod, uint32_t kind, uint32_t id0, uint32_t id1, mp_obj_t attach) {
    uint32_t i;
    if (!mp_obj_is_callable(attach)) {
        mp_raise_TypeError(MP_ERROR_TEXT("attach"));
    }
    if (s_drv_n >= PM_METAL_DRV_PY_MAX) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("PM_METAL_DRV slots full"));
    }
    i = s_drv_n++;
    copy_mod(s_drv_mod[i], sizeof(s_drv_mod[i]), mod);
    MP_STATE_VM(metal_drv_py_attach)[i] = attach;
    memset(&s_drv_rec[i], 0, sizeof(s_drv_rec[i]));
    s_drv_rec[i].mod = s_drv_mod[i];
    s_drv_rec[i].kind = kind;
    s_drv_rec[i].id0 = id0;
    s_drv_rec[i].id1 = id1;
    s_drv_rec[i].id2 = PM_METAL_DRV_PCI_ANY;
    s_drv_rec[i].id3 = PM_METAL_DRV_PCI_ANY;
    s_drv_rec[i].bar = PM_METAL_DRV_PCI_ANY;
    s_drv_rec[i].attach = s_drv_attach[i];
    if (pm_metal_drv_add(&s_drv_rec[i]) != 0) {
        MP_STATE_VM(metal_drv_py_attach)[i] = MP_OBJ_NULL;
        s_drv_n--;
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("pm_metal_drv_add"));
    }
    return 0;
}

static mp_obj_t metal_PM_METAL_DRV_PCI(size_t n_args, const mp_obj_t *args) {
    (void)n_args;
    metal_ensure();
    drv_bind(mp_obj_str_get_str(args[0]), PM_METAL_DRV_KIND_PCI,
        (uint32_t)mp_obj_get_int(args[1]), (uint32_t)mp_obj_get_int(args[2]), args[3]);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(metal_PM_METAL_DRV_PCI_obj, 4, 4, metal_PM_METAL_DRV_PCI);

static mp_obj_t metal_PM_METAL_DRV_ISA(mp_obj_t mod, mp_obj_t port, mp_obj_t attach) {
    metal_ensure();
    drv_bind(mp_obj_str_get_str(mod), PM_METAL_DRV_KIND_ISA, (uint32_t)mp_obj_get_int(port), 0,
        attach);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(metal_PM_METAL_DRV_ISA_obj, metal_PM_METAL_DRV_ISA);

static mp_obj_t metal_PM_METAL_DRV_PLATFORM(mp_obj_t mod, mp_obj_t attach) {
    metal_ensure();
    drv_bind(mp_obj_str_get_str(mod), PM_METAL_DRV_KIND_PLATFORM, 0, 0, attach);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(metal_PM_METAL_DRV_PLATFORM_obj, metal_PM_METAL_DRV_PLATFORM);

static mp_obj_t mp_metal_builtin_quit(size_t n_args, const mp_obj_t *args) {
    /* Process = task with human intent + pid. System REPL has pid 0. */
    uint32_t here = pm_metal_async_process_id();
    uint32_t want = here;
    if (n_args == 1) {
        want = (uint32_t)mp_obj_get_int(args[0]);
    }
    if (here != 0u && (n_args == 0 || want == here)) {
        mp_raise_type(&mp_type_SystemExit);
    }
    if (here == 0u) {
        if (n_args == 0) {
            mp_printf(&mp_plat_print,
                "quit() no-op: not in a process (pid 0); use shutdown() or reboot()\n");
        } else {
            mp_printf(&mp_plat_print,
                "quit(%u) no-op: not in a process (pid 0); use shutdown() or reboot()\n",
                (unsigned)want);
        }
    } else {
        mp_printf(&mp_plat_print,
            "quit(%u) no-op: not in process %u (here pid %u)\n",
            (unsigned)want, (unsigned)want, (unsigned)here);
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mp_metal_builtin_quit_obj, 0, 1, mp_metal_builtin_quit);

static mp_obj_t mp_metal_builtin_packages(void) {
    uint32_t n;
    uint32_t i;
    uint32_t nbase;
    metal_ensure();
    n = mp_wasm_local_pack_count();
    nbase = pm_wasmmod_net_cdn_base_count();

    mp_printf(&mp_plat_print, "+-- local        %u pack(s)\n", (unsigned)n);
    for (i = 0; i < n; i++) {
        const char *name = mp_wasm_local_pack_name(i);
        if (name != NULL && name[0] != 0) {
            mp_printf(&mp_plat_print, "|   %s %s\n", (i + 1u == n) ? "`--" : "+--", name);
        }
    }
    if (nbase == 0u) {
        mp_printf(&mp_plat_print, "`-- cdn          -\n");
    } else {
        mp_printf(&mp_plat_print, "`-- cdn          %u base(s)\n", (unsigned)nbase);
        for (i = 0; i < nbase; i++) {
            const char *b = pm_wasmmod_net_cdn_base_at(i);
            if (b != NULL && b[0] != 0) {
                mp_printf(&mp_plat_print, "    %s %s\n", (i + 1u == nbase) ? "`--" : "+--", b);
            }
        }
    }
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_metal_builtin_packages_obj, mp_metal_builtin_packages);

static mp_obj_t mp_metal_builtin_help(void) {
    mp_printf(&mp_plat_print,
        "MetalPython system REPL (not a process; quit/exit are no-ops here)\n"
        "  packages()     guest packs (local + cdn)\n"
        "  help()         this text\n"
        "  quit([pid]) / exit([pid])  SystemExit in a process; at pid 0 use shutdown()/reboot()\n"
        "  reboot()       reboot the seat\n"
        "  shutdown()     halt the seat\n"
        "  process()      booted module FQNs\n");
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_metal_builtin_help_obj, mp_metal_builtin_help);

static mp_obj_t mp_metal_builtin_reboot(void) {
    pm_metal_boot_shutdown(1);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_metal_builtin_reboot_obj, mp_metal_builtin_reboot);

static mp_obj_t mp_metal_builtin_shutdown(void) {
    pm_metal_boot_shutdown(0);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_metal_builtin_shutdown_obj, mp_metal_builtin_shutdown);

static mp_obj_t mp_metal_builtin_process(void) {
    uint32_t n;
    uint32_t i;
    mp_obj_t *items;
    metal_ensure();
    n = pm_mod_boot_count();
    if (n == 0) {
        return mp_obj_new_tuple(0, NULL);
    }
    items = m_new(mp_obj_t, n);
    for (i = 0; i < n; i++) {
        const char *fqn = pm_mod_boot_fqn(i);
        if (fqn == NULL) {
            fqn = "";
        }
        items[i] = mp_obj_new_str(fqn, strlen(fqn));
    }
    return mp_obj_new_tuple(n, items);
}
MP_DEFINE_CONST_FUN_OBJ_0(mp_metal_builtin_process_obj, mp_metal_builtin_process);

static const mp_rom_map_elem_t mp_module_pymergetic_metal_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pymergetic_dot_metal) },
    { MP_ROM_QSTR(MP_QSTR___init__), MP_ROM_PTR(&metal___init___obj) },
    { MP_ROM_QSTR(MP_QSTR_ready), MP_ROM_PTR(&metal_ready_obj) },
    { MP_ROM_QSTR(MP_QSTR_poll), MP_ROM_PTR(&metal_poll_obj) },
    { MP_ROM_QSTR(MP_QSTR_register_upy), MP_ROM_PTR(&metal_register_upy_obj) },
    { MP_ROM_QSTR(MP_QSTR_PM_METAL_DRV_PCI), MP_ROM_PTR(&metal_PM_METAL_DRV_PCI_obj) },
    { MP_ROM_QSTR(MP_QSTR_PM_METAL_DRV_ISA), MP_ROM_PTR(&metal_PM_METAL_DRV_ISA_obj) },
    { MP_ROM_QSTR(MP_QSTR_PM_METAL_DRV_PLATFORM), MP_ROM_PTR(&metal_PM_METAL_DRV_PLATFORM_obj) },
};
static MP_DEFINE_CONST_DICT(mp_module_pymergetic_metal_globals, mp_module_pymergetic_metal_globals_table);

const mp_obj_module_t mp_module_pymergetic_metal = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pymergetic_metal_globals,
};

#if MICROPY_MODULE_ATTR_DELEGATION
/* Fixed ROM dict cannot hold child cards; the registry resolves them. */
MP_REGISTER_MODULE_DELEGATION(mp_module_pymergetic_metal, mp_wasm_pymergetic_attr);
#endif
MP_REGISTER_MODULE(MP_QSTR_pymergetic_dot_metal, mp_module_pymergetic_metal);

PM_METAL_EXTERNAL_C(micropython, MICROPY_VERSION_STRING);
