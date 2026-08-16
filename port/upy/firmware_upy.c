/* Firmware µPy: arena malloc, ready flag, one-shot `import pymergetic.metal`. */
#include "extmod/metal/port/upy/firmware_upy.h"

#include "extmod/metal/boot.h"
#include "ports/micropython/finder.h"
#include "ports/micropython/importhook.h"
#include "pymergetic/wasmmod/pyexport/__exports__.h"
#include "py/builtin.h"
#include "py/compile.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "pymergetic/util/mem.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void uart_write(const char *s, size_t n);
void uart_puts(const char *s);

void *malloc(size_t n);
void free(void *p);
void *realloc(void *p, size_t n);

#ifndef PM_METAL_FIRMWARE_UPY_SLAB
#define PM_METAL_FIRMWARE_UPY_SLAB (12u * 1024u * 1024u)
#endif
#ifndef PM_METAL_FIRMWARE_UPY_STACK
#define PM_METAL_FIRMWARE_UPY_STACK (512u * 1024u)
#endif

typedef struct {
    size_t n;
} pm_metal_upy_hdr_t;

static pm_util_mem_arena_t *s_arena;
static int s_ready;
static uint8_t *s_bump;
static uint8_t *s_bump_end;
#ifdef PM_METAL_UEFI
static uint8_t *s_upy_stack_hi;
int pm_metal_upy_on_stack(int (*fn)(void), void *stack_hi);
static int firmware_upy_run(void);
#endif

static size_t align16(size_t n) {
    return (n + 15u) & ~(size_t)15u;
}

void pm_metal_firmware_bind_arena(pm_util_mem_arena_t *arena) {
    s_arena = arena;
    s_bump = NULL;
    s_bump_end = NULL;
#ifdef PM_METAL_UEFI
    s_upy_stack_hi = NULL;
#endif
    if (arena == NULL) {
        return;
    }
    /* TLSF grow (add_pool) after cake does not serve µPy 1KiB requests;
     * take a map() slab from the same arena hole and bump-allocate. */
    s_bump = (uint8_t *)pm_util_mem_map(arena, PM_METAL_FIRMWARE_UPY_SLAB);
    if (s_bump != NULL) {
        s_bump_end = s_bump + PM_METAL_FIRMWARE_UPY_SLAB;
#ifdef PM_METAL_UEFI
        /* EFI boot stack is ~128KiB; WAMR+hook need a real one. */
        s_upy_stack_hi = s_bump + PM_METAL_FIRMWARE_UPY_STACK;
        s_bump = s_upy_stack_hi;
#endif
    }
}

void pm_metal_set_ready(int v) {
    s_ready = v;
}

int pm_metal_ready(void) {
    return s_ready;
}

int pm_metal_boot(void) {
    return s_ready ? 0 : -1;
}

void *pm_metal_wasm_malloc(size_t n) {
    pm_metal_upy_hdr_t *h;
    size_t need;
    if (n == 0) {
        n = 1;
    }
    n = align16(n);
    need = sizeof(*h) + n;
    if (s_bump == NULL || s_bump + need > s_bump_end) {
        return NULL;
    }
    h = (pm_metal_upy_hdr_t *)(void *)s_bump;
    h->n = n;
    s_bump += need;
    return h + 1;
}

void pm_metal_wasm_free(void *p) {
    (void)p;
}

void *pm_metal_wasm_realloc(void *p, size_t n) {
    void *q;
    size_t old;
    if (p == NULL) {
        return pm_metal_wasm_malloc(n);
    }
    if (n == 0) {
        return NULL;
    }
    old = ((pm_metal_upy_hdr_t *)p - 1)->n;
    q = pm_metal_wasm_malloc(n);
    if (q == NULL) {
        return NULL;
    }
    memcpy(q, p, old < n ? old : n);
    return q;
}

void *malloc(size_t n) {
    return pm_metal_wasm_malloc(n);
}

void free(void *p) {
    pm_metal_wasm_free(p);
}

void *realloc(void *p, size_t n) {
    return pm_metal_wasm_realloc(p, n);
}

void *calloc(size_t nmemb, size_t size) {
    size_t n;
    void *p;
    if (nmemb != 0 && size > ((size_t)-1) / nmemb) {
        return NULL;
    }
    n = nmemb * size;
    p = malloc(n);
    if (p != NULL) {
        memset(p, 0, n);
    }
    return p;
}

int pm_wasmmod_pyexport_bind_module(const char *fqn, pm_wasmmod_py_obj_t module) {
    (void)fqn;
    (void)module;
    return 0;
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    uart_write(str, len);
    return (mp_uint_t)len;
}

int mp_hal_stdin_rx_chr(void) {
    return -1;
}

#if !MICROPY_VFS
mp_import_stat_t mp_import_stat(const char *path) {
    (void)path;
    return MP_IMPORT_STAT_NO_EXIST;
}

mp_lexer_t *mp_lexer_new_from_file(qstr filename) {
    (void)filename;
    mp_raise_OSError(MP_ENOENT);
}
#endif

void nlr_jump_fail(void *val) {
    (void)val;
    uart_puts("nlr fail\n");
    for (;;) {
        __asm__ volatile("hlt");
    }
}

#ifdef PM_METAL_UEFI
int pm_metal_firmware_upy(void) {
    if (s_bump == NULL) {
        uart_puts("upy slab\n");
        return -1;
    }
    if (s_upy_stack_hi == NULL) {
        uart_puts("upy stack\n");
        return -1;
    }
    return pm_metal_upy_on_stack(firmware_upy_run, s_upy_stack_hi);
}

static int firmware_upy_run(void)
#else
int pm_metal_firmware_upy(void)
#endif
{
    static const char src[] = "import pymergetic.metal as m\n"
                              "if not m.ready():\n"
                              "    raise RuntimeError('ready')\n"
                              "print('upy metal ready')\n"
                              "import pymergetic.wasmmod.net.cdn as cdn\n"
                              "cdn.session_id('sess-1')\n"
                              "cdn.configure('http://10.0.2.2:1', 'tok-cdn')\n"
                              "print('upy cdn')\n"
                              "import pymergetic.wasmmod_examples.hello as hello\n"
                              "print('upy pack import')\n";
    extern const uint8_t *pm_metal_hello_wasm_bytes(void);
    extern unsigned pm_metal_hello_wasm_size(void);
    nlr_buf_t nlr;
    if (s_bump == NULL) {
        uart_puts("upy slab\n");
        return -1;
    }
    mp_stack_ctrl_init();
    mp_stack_set_limit(384u * 1024u);
    mp_init();
    if (nlr_push(&nlr) == 0) {
        /* Don't wait on ROM pymergetic.__init__ — UEFI COFF can skip it. */
        mp_wasm_ensure_inited();
        mp_wasm_register_local_bytes("pymergetic.wasmmod_examples.hello",
            pm_metal_hello_wasm_bytes(), pm_metal_hello_wasm_size());
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, sizeof(src) - 1u, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t fn = mp_compile(&tree, source_name, false);
        mp_call_function_0(fn);
        nlr_pop();
#ifdef PM_METAL_UEFI
        /* WAMR teardown on the EFI return path #UDs; this seat halts next. */
#else
        mp_deinit();
#endif
        return 0;
    }
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
#ifdef PM_METAL_UEFI
    return -1;
#else
    mp_deinit();
    return -1;
#endif
}
