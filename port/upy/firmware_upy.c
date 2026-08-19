/* Firmware µPy: bump slab + one-shot guest. Ready comes from pm_metal_boot(). */
#include "extmod/metal/port/upy/firmware_upy.h"

#include "pymergetic/metal/boot.h"
#include "pymergetic/metal/console.h"
#include "pymergetic/metal/net/http/asgi.h"
#include "pymergetic/metal/net/ssh.h"
#include "ports/micropython/finder.h"
#include "ports/micropython/importhook.h"
#include "pymergetic/wasmmod/pyexport.h"
#include "py/builtin.h"
#include "py/compile.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "pymergetic/util/mem.h"
#if MICROPY_HELPER_REPL
#include "shared/readline/readline.h"
#include "shared/runtime/pyexec.h"
#endif

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void uart_write(const char *s, size_t n);
void uart_puts(const char *s);
int uart_rx_chr(void);

/* modmetal.c — mark the /packs page renderer to start from the MOTD surface
 * (a firmware seat brings its listeners up itself, so it declares the flag the
 * banner hook consumes). The root boot.h declares these for the hosted port;
 * the firmware port includes the module umbrella instead, so redeclare. */
void mp_metal_packs_autostart(void);

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
    /* TLSF grow (add_pool) after bring-up does not serve µPy 1KiB requests;
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

void pm_metal_boot_fill_bind_arena(pm_util_mem_arena_t *arena) {
    pm_metal_firmware_bind_arena(arena);
}

const char *pm_metal_boot_fill_map_label(void) {
    return "upy";
}

/* Firmware boots onto a real wire — QEMU user-net or a lab LAN — where the
 * address is the server's to give, not ours to invent. */
int32_t pm_metal_boot_fill_want_dhcp(void) {
    return 1;
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
    uint32_t n = len > 0xffffffffu ? 0xffffffffu : (uint32_t)len;
    (void)pm_metal_console_write(str, n);
    return (mp_uint_t)n;
}

int mp_hal_stdin_rx_chr(void) {
    return uart_rx_chr();
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
#if defined(__i386__) || defined(__x86_64__)
        __asm__ volatile("hlt");
#else
        __asm__ volatile("wfi");
#endif
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
    /* Guest autoexec (prove / first script). MOTD is pm_metal_boot_motd(),
     * not this file — same painter as the REPL banner hook. */
    extern const char *pm_metal_firmware_upy_ready_py(void);
    extern unsigned pm_metal_firmware_upy_ready_py_len(void);
    extern const char *pm_metal_firmware_upy_cdn_py(void);
    extern unsigned pm_metal_firmware_upy_cdn_py_len(void);
    extern const uint8_t *pm_metal_hello_wasm_bytes(void);
    extern unsigned pm_metal_hello_wasm_size(void);
    const char *src;
    unsigned src_len;
    nlr_buf_t nlr;
    /* Autoexec choice. The interactive REPL seat ('run', REPL=1) uses the ready
     * autoexec: it has no CDN fetch, so boot reaches the REPL + auto-served
     * httpd/sshd regardless of whether any host pack server is up. The prove
     * seat (REPL=0) runs the full CDN autoexec under the live host CDN. ARM
     * hardware has no QEMU CDN (10.0.2.2), so it always uses ready. */
#if defined(__arm__) || defined(__ARM_ARCH) || MICROPY_HELPER_REPL
    src = pm_metal_firmware_upy_ready_py();
    src_len = pm_metal_firmware_upy_ready_py_len();
#else
    src = pm_metal_firmware_upy_cdn_py();
    src_len = pm_metal_firmware_upy_cdn_py_len();
#endif
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
        mp_lexer_t *lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, src, src_len, 0);
        qstr source_name = lex->source_name;
        mp_parse_tree_t tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t fn = mp_compile(&tree, source_name, false);
        mp_call_function_0(fn);
        nlr_pop();
    } else {
        mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
#if !MICROPY_HELPER_REPL
#ifdef PM_METAL_UEFI
        return -1;
#else
        mp_deinit();
        return -1;
#endif
#endif
    }
#if MICROPY_HELPER_REPL
    readline_init0();
    /* Interactive run seat (REPL=1): bring up the inspect httpd (:8090) and the
     * ssh console (:2222) so the box serves the moment it boots. Both listeners
     * are idempotent — already-listening is not an error. ANY (0) makes them
     * accept on the DHCP'd NIC address (10.0.2.15 under QEMU user-net), so the
     * QEMU hostfwd rules map them to the host. REPL=0 prove seats skip this
     * block entirely and stay listener-free. */
    int32_t _http = pm_metal_net_http_asgi_listen(0u, 8090);
    int32_t _ssh = pm_metal_net_ssh_listen(0u, 2222);
    mp_printf(&mp_plat_print, "serve httpd=%d ssh=%d\n", (int)_http, (int)_ssh);
    /* Mark the page renderer to start from the MOTD/banner surface (which the
     * REPL banner hook below re-walks once the VM is up) — the same single
     * surface every seat starts it from. Callers of mp/metal_packs start() do
     * their own import, so nothing needs pre-publishing into sys.modules. */
    mp_metal_packs_autostart();
    for (;;) {
        /* System REPL: Ctrl-D re-enters. shutdown()/reboot() halt. */
        (void)pyexec_friendly_repl();
    }
#else
    /* Prove seat: guest autoexec already ran; same MOTD as the REPL banner hook. */
    pm_metal_boot_motd();
#endif
#ifdef PM_METAL_UEFI
    /* WAMR teardown on the EFI return path #UDs; this seat halts next. */
#else
    mp_deinit();
#endif
    return 0;
}
