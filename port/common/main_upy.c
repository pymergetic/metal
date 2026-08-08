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
#include "pymergetic/metal/net/upy_nic/__init__.h"
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
#if METAL_BOARD_UEFI
        /* UEFI: FrameBuffer() #UD (SSE/copy path); BIOS still covers framebuf. */
        uart_puts("framebuf skip\n");
#else
        do_str(
            "import framebuf\n"
            "b=bytearray(64)\n"
            "f=framebuf.FrameBuffer(b,16,8,framebuf.MVLSB)\n"
            "f.fill(1)\n"
            "f.pixel(0,0,0)\n"
            "print('framebuf ok')\n",
            MP_PARSE_FILE_INPUT);
#endif
#endif
        /* SSH µPy face (stub until real backend). */
        do_str(
            "import ssh\n"
            "if ssh.available():\n"
            "  assert ssh.__version__\n"
            "  assert 'ssh' in ssh.info\n"
            "  assert ssh.init()==0\n"
            "  print('ssh py ok')\n"
            "else:\n"
            "  print('ssh stub')\n",
            MP_PARSE_FILE_INPUT);
#if MICROPY_PY_NETWORK
        do_str(
            "import network\n"
            "n=network.LAN()\n"
            "assert n.active()\n"
            "assert n.isconnected()\n"
            "c=n.ifconfig()\n"
            "assert c[0]!='0.0.0.0'\n"
            "print('network ok')\n"
            "assert n.resolve('10.0.2.2')=='10.0.2.2'\n"
            "a=''\n"
            "for i in range(3):\n"
            "  try:\n"
            "    a=n.resolve('example.com')\n"
            "    if a and a!='0.0.0.0':\n"
            "      break\n"
            "  except OSError:\n"
            "    pass\n"
            "assert a and a!='0.0.0.0'\n"
            "print('dns py ok')\n"
            "import socket\n"
            "ai=socket.getaddrinfo(a,80)[0][-1]\n"
            "ok=0\n"
            "for i in range(4):\n"
            "  s=None\n"
            "  try:\n"
            "    s=socket.socket()\n"
            "    s.connect(ai)\n"
            "    s.send(b'GET / HTTP/1.0\\r\\nHost: example.com\\r\\nConnection: close\\r\\n\\r\\n')\n"
            "    d=s.recv(128)\n"
            "    if d and d[:5]==b'HTTP/':\n"
            "      ok=1\n"
            "  except OSError:\n"
            "    pass\n"
            "  finally:\n"
            "    if s:\n"
            "      try:\n"
            "        s.close()\n"
            "      except OSError:\n"
            "        pass\n"
            "  if ok:\n"
            "    break\n"
            "assert ok\n"
            "print('socket ok')\n",
            MP_PARSE_FILE_INPUT);
#endif
#if MICROPY_MODULE_FROZEN_MPY
        do_str(
            "import microdot\n"
            "assert microdot.__version__\n"
            "from microdot import Microdot\n"
            "assert Microdot is not None\n"
            "print('microdot ok')\n"
            "from pymergetic.metal.inspect.dispatch import handle\n"
            "st, body = handle('GET', '/health')\n"
            "assert st == 200 and 'ok' in body\n"
            "st, body = handle('GET', '/capabilities')\n"
            "assert st == 200 and 'metal' in body\n"
            "st, body = handle('GET', '/inspect/self')\n"
            "assert st == 200 and 'kernel' in body and 'has_source' in body\n"
            "assert handle('GET', '/inspect/') is None\n"
            "print('inspect py ok')\n",
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
