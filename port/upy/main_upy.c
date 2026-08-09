/*
 * Shared µPy bring-up after board UART is live.
 * Called from board platform_main / pm_metal_bios_main.
 *
 * No embedded Python source (do_str). Smoke = reg_run_tests();
 * product = C callees (autoexec, wasmmod hook, arch.autoexec).
 */
#include <stdint.h>
#include <string.h>

#include "py/builtin.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/nlr.h"
#include "py/runtime.h"
#include "shared/runtime/pyexec.h"

#include "mphalport.h"
#include "pymergetic/metal/boot/product.h"
#include "pymergetic/metal/reg/mod.h"
#include "pymergetic/metal/reg/seats.h"

#include "pm_upy/obj/call.h"

#ifndef METAL_LIVE
#define METAL_LIVE 0
#endif
#ifndef METAL_LIVE_SSH
#define METAL_LIVE_SSH 0
#endif
#ifndef METAL_BOARD_UEFI
#define METAL_BOARD_UEFI 0
#endif

#if MICROPY_PY_NETWORK
#include "extmod/modnetwork.h"
#include "pymergetic/metal/net/nic/__init__.h"
#endif

#if MICROPY_ENABLE_GC
static char heap[MICROPY_HEAP_SIZE] __attribute__((aligned(16)));
#endif

static char *stack_top;

static void call_upy_fn0(const char *dotted)
{
    uint32_t h = pm_upy_fn_resolve(dotted);
    if (h != 0) {
        (void)pm_upy_fn_call(h, 0, NULL);
    }
}

void mp_metal_upy_run(int smoke) {
    int stack_dummy;
    stack_top = (char *)&stack_dummy;

#if MICROPY_ENABLE_GC
    gc_init(heap, heap + sizeof(heap));
#endif
    mp_init();

    /* Mutable metal/net/boot/dev globals so frozen CORE can bind (import store_attr). */
    void pm_metal_globals_init(void);
    pm_metal_globals_init();
#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER
    void pm_metal_net_globals_init(void);
    void pm_metal_boot_globals_init(void);
    void pm_metal_dev_globals_init(void);
    void pm_metal_fs_globals_init(void);
    pm_metal_net_globals_init();
    pm_metal_boot_globals_init();
    pm_metal_dev_globals_init();
    pm_metal_fs_globals_init();
#endif

#if MICROPY_PY_NETWORK
    mod_network_init();
#if MICROPY_PY_LWIP
    mod_network_lwip_init();
#endif
    (void)pm_metal_net_nic_attach_upy();
#endif

    /* Floor seats + co-located tests — same store for smoke and REPL/Inspect. */
    pm_metal_reg_seats_boot();
    {
        extern void pm_metal_smoke_register_seats(void);
        pm_metal_smoke_register_seats();
    }
    /* Permanently-linked RegMods (exports first, then connect_all). */
    (void)pm_metal_reg_floor_load();

    if (smoke) {
        if (pm_metal_reg_run_tests() != 0) {
            uart_puts("reg tests fail\n");
            return;
        }
        uart_puts("qemu ok\n");
        return;
    }

#if MICROPY_ENABLE_COMPILER
    /* After mp_init: bind CDN (all seats), then thin arch epilogue. */
    (void)pm_metal_autoexec();
#if defined(MICROPY_PY_WASM) && MICROPY_PY_WASM
    call_upy_fn0("pymergetic.wasmmod.install_hook");
#endif
#if MICROPY_MODULE_FROZEN_MPY
    /* arch.autoexec() installs quit/exit then runs the seat epilogue. */
    call_upy_fn0("pymergetic.metal.arch.autoexec");
#endif
    pyexec_friendly_repl();
#else
    uart_puts("upy: compiler disabled\n");
#endif
    mp_deinit();
}

#if MICROPY_ENABLE_GC
void gc_collect(void) {
    void *dummy;
    gc_collect_start();
    gc_collect_root(&dummy, ((mp_uint_t)stack_top - (mp_uint_t)&dummy) / sizeof(mp_uint_t));
    gc_collect_end();
}
#endif

mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    (void)filename;
    mp_raise_OSError(MP_ENOENT);
}

#if MICROPY_PY_IO
mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs) {
    (void)n_args;
    (void)args;
    (void)kwargs;
    mp_raise_OSError(MP_ENOENT);
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);
#endif

void nlr_jump_fail(void *val) {
    (void)val;
    uart_puts("nlr_jump_fail\n");
    for (;;) {
    }
}

void __fatal_error(const char *msg) {
    uart_puts("FATAL: ");
    uart_puts(msg ? msg : "?");
    uart_puts("\n");
    for (;;) {
    }
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)file;
    (void)line;
    (void)func;
    (void)expr;
    __fatal_error("Assertion failed");
}
#endif
