/*
 * Shared µPy bring-up after board UART is live.
 * Called from board platform_main / pm_metal_bios_main.
 */
#include <stdint.h>
#include <string.h>

#include "py/builtin.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "shared/runtime/pyexec.h"

#include "mphalport.h"

#if MICROPY_PY_NETWORK
#include "extmod/modnetwork.h"
#include "pymergetic/metal/net/upy_nic.h"
#endif

#if MICROPY_ENABLE_GC
static char heap[MICROPY_HEAP_SIZE] __attribute__((aligned(16)));
#endif

static char *stack_top;

#if MICROPY_ENABLE_COMPILER
static void do_str(const char *src, mp_parse_input_kind_t input_kind) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, strlen(src), 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, true);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, (mp_obj_t)nlr.ret_val);
    }
}
#endif

void mp_metal_upy_run(int smoke) {
    int stack_dummy;
    stack_top = (char *)&stack_dummy;

#if MICROPY_ENABLE_GC
    gc_init(heap, heap + sizeof(heap));
#endif
    mp_init();

#if MICROPY_PY_NETWORK
    mod_network_init();
    (void)pm_metal_net_upy_nic_attach_upy();
#endif

    if (smoke) {
#if MICROPY_ENABLE_COMPILER
#if MICROPY_PY_FRAMEBUF
        do_str(
            "import framebuf\n"
            "b=bytearray(64)\n"
            "f=framebuf.FrameBuffer(b,16,8,framebuf.MVLSB)\n"
            "f.fill(1)\n"
            "f.pixel(0,0,0)\n"
            "print('framebuf ok')\n",
            MP_PARSE_FILE_INPUT);
#endif
#if MICROPY_PY_NETWORK
        do_str(
            "import network\n"
            "n=network.LAN()\n"
            "assert n.active()\n"
            "assert n.isconnected()\n"
            "c=n.ifconfig()\n"
            "assert c[0]!='0.0.0.0'\n"
            "print('network ok')\n",
            MP_PARSE_FILE_INPUT);
#endif
        do_str("print('upy ok')", MP_PARSE_SINGLE_INPUT);
#endif
        uart_puts("qemu ok\n");
        return;
    }

#if MICROPY_ENABLE_COMPILER
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

void nlr_jump_fail(void *val) {
    (void)val;
    uart_puts("nlr_jump_fail\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void MP_NORETURN __fatal_error(const char *msg) {
    uart_puts("FATAL: ");
    uart_puts(msg);
    uart_puts("\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    (void)file;
    (void)line;
    (void)func;
    (void)expr;
    __fatal_error("assert");
}
#endif
