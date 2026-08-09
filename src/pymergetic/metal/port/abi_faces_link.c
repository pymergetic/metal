/*
 * Product link faces for seats whose muscle lives in RS/C sources not yet
 * pulled into RUST_LIBS / board OBJ. Keeps µPy glue resolvable on all FW
 * boards (BIOS + UEFI). Prefer real crates/objs when those seats join the
 * product image.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/blk/__init__.h>
#include <pymergetic/metal/dev/stream/__init__.h>
#include <pymergetic/metal/fs/embed/__init__.h>
#include <pymergetic/metal/fs/fat/__init__.h>
#include <pymergetic/metal/fs/littlefs/__init__.h>
#include <pymergetic/metal/fs/overlay/__init__.h>
#include <pymergetic/metal/fs/zip/__init__.h>
#include <pymergetic/metal/hwtree/__init__.h>
#include <pymergetic/metal/mem/arena/__init__.h>
#include <pymergetic/metal/mem/lock/__init__.h>
#include <pymergetic/metal/mem/tlsf/__init__.h>
#include <pymergetic/metal/wamr_host/__init__.h>

/* Conte TLSF already linked as metal_tlsf.o */
typedef void *tlsf_t;
typedef void *pool_t;
extern tlsf_t tlsf_create(void *mem);
extern tlsf_t tlsf_create_with_pool(void *mem, size_t bytes);
extern void tlsf_destroy(tlsf_t tlsf);
extern pool_t tlsf_get_pool(tlsf_t tlsf);
extern pool_t tlsf_add_pool(tlsf_t tlsf, void *mem, size_t bytes);
extern void tlsf_remove_pool(tlsf_t tlsf, pool_t pool);
extern void *tlsf_malloc(tlsf_t tlsf, size_t bytes);
extern void *tlsf_memalign(tlsf_t tlsf, size_t align, size_t bytes);
extern void *tlsf_realloc(tlsf_t tlsf, void *ptr, size_t size);
extern void tlsf_free(tlsf_t tlsf, void *ptr);
extern size_t tlsf_block_size(void *ptr);
extern size_t tlsf_size(void);
extern size_t tlsf_align_size(void);
extern size_t tlsf_block_size_min(void);
extern size_t tlsf_block_size_max(void);
extern size_t tlsf_pool_overhead(void);
extern size_t tlsf_alloc_overhead(void);
extern int tlsf_check(tlsf_t tlsf);
extern int tlsf_check_pool(pool_t pool);

size_t pm_metal_mem_tlsf_size(void) { return tlsf_size(); }
size_t pm_metal_mem_tlsf_pool_overhead(void) { return tlsf_pool_overhead(); }
size_t pm_metal_mem_tlsf_align_size(void) { return tlsf_align_size(); }
size_t pm_metal_mem_tlsf_alloc_overhead(void) { return tlsf_alloc_overhead(); }
void *pm_metal_mem_tlsf_create_with_pool(uint8_t *mem, size_t bytes)
{
    return tlsf_create_with_pool(mem, bytes);
}
void *pm_metal_mem_tlsf_get_pool(void *t) { return tlsf_get_pool(t); }
void *pm_metal_mem_tlsf_add_pool(void *t, uint8_t *mem, size_t bytes)
{
    return tlsf_add_pool(t, mem, bytes);
}
uint8_t *pm_metal_mem_tlsf_malloc(void *t, size_t size)
{
    return (uint8_t *)tlsf_malloc(t, size);
}
uint8_t *pm_metal_mem_tlsf_memalign(void *t, size_t align, size_t size)
{
    return (uint8_t *)tlsf_memalign(t, align, size);
}
uint8_t *pm_metal_mem_tlsf_realloc(void *t, uint8_t *ptr, size_t size)
{
    return (uint8_t *)tlsf_realloc(t, ptr, size);
}
void pm_metal_mem_tlsf_free(void *t, uint8_t *p) { tlsf_free(t, p); }
size_t pm_metal_mem_tlsf_block_size(uint8_t *ptr) { return tlsf_block_size(ptr); }
size_t pm_metal_mem_tlsf_block_size_min(void) { return tlsf_block_size_min(); }
size_t pm_metal_mem_tlsf_block_size_max(void) { return tlsf_block_size_max(); }
void *pm_metal_mem_tlsf_create(uint8_t *mem) { return tlsf_create(mem); }
void pm_metal_mem_tlsf_destroy(void *t) { tlsf_destroy(t); }
void pm_metal_mem_tlsf_remove_pool(void *t, void *pool) { tlsf_remove_pool(t, pool); }
void pm_metal_mem_tlsf_walk_pool(void *pool, pm_metal_mem_tlsf_walker_fn walker, void *user)
{
    (void)pool;
    (void)walker;
    (void)user;
}
int32_t pm_metal_mem_tlsf_check(void *t) { return (int32_t)tlsf_check(t); }
int32_t pm_metal_mem_tlsf_check_pool(void *pool) { return (int32_t)tlsf_check_pool(pool); }

void pm_metal_mem_lock_mutex_init(pm_metal_mem_lock_mutex_t *m)
{
    if (m) {
        m->state = 0;
    }
}
void pm_metal_mem_lock_mutex_lock(const pm_metal_mem_lock_mutex_t *m)
{
    pm_metal_mem_lock_mutex_t *mut = (pm_metal_mem_lock_mutex_t *)(uintptr_t)m;
    if (!mut) {
        return;
    }
    while (mut->state != 0) {
    }
    mut->state = 1;
}
int32_t pm_metal_mem_lock_mutex_try_lock(const pm_metal_mem_lock_mutex_t *m)
{
    pm_metal_mem_lock_mutex_t *mut = (pm_metal_mem_lock_mutex_t *)(uintptr_t)m;
    if (!mut || mut->state != 0) {
        return 0;
    }
    mut->state = 1;
    return 1;
}
void pm_metal_mem_lock_mutex_unlock(const pm_metal_mem_lock_mutex_t *m)
{
    pm_metal_mem_lock_mutex_t *mut = (pm_metal_mem_lock_mutex_t *)(uintptr_t)m;
    if (mut) {
        mut->state = 0;
    }
}
void pm_metal_mem_lock_spin_init(pm_metal_mem_lock_spin_t *s)
{
    if (s) {
        s->state = 0;
    }
}
void pm_metal_mem_lock_spin_lock(const pm_metal_mem_lock_spin_t *s)
{
    pm_metal_mem_lock_spin_t *sp = (pm_metal_mem_lock_spin_t *)(uintptr_t)s;
    if (!sp) {
        return;
    }
    while (sp->state != 0) {
    }
    sp->state = 1;
}
int32_t pm_metal_mem_lock_spin_try_lock(const pm_metal_mem_lock_spin_t *s)
{
    pm_metal_mem_lock_spin_t *sp = (pm_metal_mem_lock_spin_t *)(uintptr_t)s;
    if (!sp || sp->state != 0) {
        return 0;
    }
    sp->state = 1;
    return 1;
}
void pm_metal_mem_lock_spin_unlock(const pm_metal_mem_lock_spin_t *s)
{
    pm_metal_mem_lock_spin_t *sp = (pm_metal_mem_lock_spin_t *)(uintptr_t)s;
    if (sp) {
        sp->state = 0;
    }
}

pm_metal_mem_arena_t pm_metal_mem_arena_empty(void)
{
    pm_metal_mem_arena_t a;
    memset(&a, 0, sizeof(a));
    return a;
}
int32_t pm_metal_mem_arena_init(pm_metal_mem_arena_t *a, uint8_t *base, size_t bytes)
{
    if (!a || !base || bytes == 0) {
        return -1;
    }
    a->base = (size_t)(uintptr_t)base;
    a->end = a->base + bytes;
    a->map_brk = a->base;
    a->heap_brk = a->end;
    a->lock = 0;
    return 0;
}
int32_t pm_metal_mem_arena_ready(const pm_metal_mem_arena_t *a)
{
    return (a && a->base && a->end > a->base) ? 1 : 0;
}
size_t pm_metal_mem_arena_bytes(const pm_metal_mem_arena_t *a)
{
    return a ? (a->end - a->base) : 0;
}
size_t pm_metal_mem_arena_map_used(const pm_metal_mem_arena_t *a)
{
    return a ? (a->map_brk - a->base) : 0;
}
size_t pm_metal_mem_arena_heap_used(const pm_metal_mem_arena_t *a)
{
    return a ? (a->end - a->heap_brk) : 0;
}
size_t pm_metal_mem_arena_hole(const pm_metal_mem_arena_t *a)
{
    return a ? (a->heap_brk - a->map_brk) : 0;
}
uint8_t *pm_metal_mem_arena_heap_grow(pm_metal_mem_arena_t *a, size_t bytes)
{
    (void)a;
    (void)bytes;
    return NULL;
}
uint8_t *pm_metal_mem_arena_map(pm_metal_mem_arena_t *a, size_t bytes)
{
    (void)a;
    (void)bytes;
    return NULL;
}
int32_t pm_metal_mem_arena_unmap(pm_metal_mem_arena_t *a, uint8_t *ptr, size_t bytes)
{
    (void)a;
    (void)ptr;
    (void)bytes;
    return -1;
}
size_t pm_metal_mem_arena_align_up(size_t x, size_t al)
{
    return al ? ((x + al - 1) & ~(al - 1)) : x;
}
size_t pm_metal_mem_arena_align_down(size_t x, size_t al)
{
    return al ? (x & ~(al - 1)) : x;
}
size_t pm_metal_mem_arena_page_size(void) { return 4096u; }

int32_t pm_metal_dev_blk_detect(void) { return 0; }
int32_t pm_metal_dev_blk_open(void) { return -1; }
uint64_t pm_metal_dev_blk_capacity_sectors(void) { return 0; }
int32_t pm_metal_dev_blk_read(uint64_t lba, void *buf, uint32_t nsec)
{
    (void)lba;
    (void)buf;
    (void)nsec;
    return -1;
}
uint32_t pm_metal_dev_blk_read_async(uint64_t lba, void *buf, uint32_t nsec)
{
    (void)lba;
    (void)buf;
    (void)nsec;
    return 0;
}
uint32_t pm_metal_dev_blk_result(uint32_t h)
{
    (void)h;
    return 0;
}

int32_t pm_metal_stream_pipe(pm_metal_stream_h *read_end, pm_metal_stream_h *write_end)
{
    if (read_end) {
        *read_end = PM_METAL_STREAM_INVALID;
    }
    if (write_end) {
        *write_end = PM_METAL_STREAM_INVALID;
    }
    return -1;
}
int32_t pm_metal_stream_pty(pm_metal_stream_h *master, pm_metal_stream_h *slave)
{
    if (master) {
        *master = PM_METAL_STREAM_INVALID;
    }
    if (slave) {
        *slave = PM_METAL_STREAM_INVALID;
    }
    return -1;
}
uint32_t pm_metal_stream_write(pm_metal_stream_h h, const void *ptr, uint32_t len)
{
    (void)h;
    (void)ptr;
    (void)len;
    return 0;
}
uint32_t pm_metal_stream_try_read(pm_metal_stream_h h, void *ptr, uint32_t len)
{
    (void)h;
    (void)ptr;
    (void)len;
    return 0;
}
uint32_t pm_metal_stream_read(pm_metal_stream_h h, void *ptr, uint32_t len)
{
    (void)h;
    (void)ptr;
    (void)len;
    return 0;
}
uint32_t pm_metal_stream_drain(pm_metal_stream_h h)
{
    (void)h;
    return 0;
}
void pm_metal_stream_close(pm_metal_stream_h h) { (void)h; }

int32_t pm_metal_stream_termios_get(pm_metal_stream_h h, pm_metal_stream_termios_t *out)
{
    (void)h;
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return -1;
}
int32_t pm_metal_stream_termios_set(pm_metal_stream_h h, const pm_metal_stream_termios_t *in)
{
    (void)h;
    (void)in;
    return -1;
}
int32_t pm_metal_stream_winsize_get(pm_metal_stream_h h, pm_metal_stream_winsize_t *out)
{
    (void)h;
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return -1;
}
int32_t pm_metal_stream_winsize_set(pm_metal_stream_h h, const pm_metal_stream_winsize_t *in)
{
    (void)h;
    (void)in;
    return -1;
}
uint32_t pm_metal_stream_pending(pm_metal_stream_h h)
{
    (void)h;
    return 0;
}

int32_t pm_metal_stdio_attach(pm_metal_stream_h in, pm_metal_stream_h out, pm_metal_stream_h err)
{
    (void)in;
    (void)out;
    (void)err;
    return -1;
}
pm_metal_stream_h pm_metal_stdio_in(void) { return PM_METAL_STREAM_INVALID; }
pm_metal_stream_h pm_metal_stdio_out(void) { return PM_METAL_STREAM_INVALID; }
pm_metal_stream_h pm_metal_stdio_err(void) { return PM_METAL_STREAM_INVALID; }
uint32_t pm_metal_stream_feed_stdin(const void *ptr, uint32_t len)
{
    (void)ptr;
    (void)len;
    return 0;
}
uint32_t pm_metal_stream_write_line(pm_metal_stream_h h, const char *line)
{
    (void)h;
    (void)line;
    return 0;
}

int32_t pm_metal_fs_fat_format_buf(uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return -1;
}
uint32_t pm_metal_fs_fat_open_buf(uint8_t *buf, size_t len)
{
    (void)buf;
    (void)len;
    return 0;
}
int32_t pm_metal_fs_fat_close(uint32_t vol)
{
    (void)vol;
    return -1;
}
int32_t pm_metal_fs_fat_mount(const uint8_t *target, uint8_t *buf, size_t len)
{
    (void)target;
    (void)buf;
    (void)len;
    return -1;
}
int32_t pm_metal_fs_fat_seed_simple(uint8_t *buf, size_t len, const uint8_t *const *names,
                                    const uint8_t *const *datas, const uint32_t *lens,
                                    uint32_t count)
{
    (void)buf;
    (void)len;
    (void)names;
    (void)datas;
    (void)lens;
    (void)count;
    return -1;
}
int32_t pm_metal_fs_fat_mount_ram(const uint8_t *target, uint32_t ram_h)
{
    (void)target;
    (void)ram_h;
    return -1;
}

int32_t pm_metal_fs_embed_c(const uint8_t *name, const uint8_t *data, size_t len, uint8_t *out,
                            size_t out_cap, size_t *out_len)
{
    (void)name;
    (void)data;
    (void)len;
    (void)out;
    (void)out_cap;
    if (out_len) {
        *out_len = 0;
    }
    return -1;
}
int32_t pm_metal_fs_embed_rs(const uint8_t *name, const uint8_t *data, size_t len, uint8_t *out,
                             size_t out_cap, size_t *out_len)
{
    (void)name;
    (void)data;
    (void)len;
    (void)out;
    (void)out_cap;
    if (out_len) {
        *out_len = 0;
    }
    return -1;
}

uint32_t pm_metal_fs_zip_open_blob(const uint8_t *blob, size_t len)
{
    (void)blob;
    (void)len;
    return 0;
}
int32_t pm_metal_fs_zip_mount(const uint8_t *target, const uint8_t *blob, size_t len)
{
    (void)target;
    (void)blob;
    (void)len;
    return -1;
}
int32_t pm_metal_fs_zip_pack_simple(const uint8_t *const *names, const uint8_t *const *datas,
                                    const uint32_t *lens, uint32_t count, uint8_t *out,
                                    size_t out_cap, size_t *out_len)
{
    (void)names;
    (void)datas;
    (void)lens;
    (void)count;
    (void)out;
    (void)out_cap;
    if (out_len) {
        *out_len = 0;
    }
    return -1;
}
int32_t pm_metal_fs_zip_empty(uint8_t *out, size_t out_cap, size_t *out_len)
{
    (void)out;
    (void)out_cap;
    if (out_len) {
        *out_len = 0;
    }
    return -1;
}

int32_t pm_metal_fs_littlefs_mount(const uint8_t *target, uint8_t *buf, size_t len)
{
    (void)target;
    (void)buf;
    (void)len;
    return -1;
}

int32_t pm_metal_fs_overlay_mount(const uint8_t *target, const pm_metal_fs_ops_t *lower_ops,
                                  void *lower_ctx, const pm_metal_fs_ops_t *upper_ops,
                                  void *upper_ctx)
{
    (void)target;
    (void)lower_ops;
    (void)lower_ctx;
    (void)upper_ops;
    (void)upper_ctx;
    return -1;
}

int32_t pm_metal_hwtree_print(void) { return 0; }

int32_t pm_metal_wasm_fetch_register(const uint8_t *full_module, const char *url, const uint8_t *sig,
                                     uint32_t sig_len)
{
    (void)full_module;
    (void)url;
    (void)sig;
    (void)sig_len;
    return -1;
}
int32_t pm_metal_wasm_proof_fetch(void) { return -1; }
uint32_t pm_metal_wasm_guest_coro_create_for(const uint8_t *full_module, uint32_t state_bytes)
{
    (void)full_module;
    (void)state_bytes;
    return 0;
}
int32_t pm_metal_wasm_ready(void) { return 0; }
int32_t pm_metal_wasm_init(void) { return -1; }
void pm_metal_wasm_shutdown(void) {}
int32_t pm_metal_wasm_load(const uint8_t *full_module, const uint8_t *bytes, uint32_t len)
{
    (void)full_module;
    (void)bytes;
    (void)len;
    return -1;
}
int32_t pm_metal_wasm_image(const uint8_t *full_module, const uint8_t **out_bytes, uint32_t *out_len)
{
    (void)full_module;
    if (out_bytes) {
        *out_bytes = NULL;
    }
    if (out_len) {
        *out_len = 0;
    }
    return -1;
}
int32_t pm_metal_wasm_register(const uint8_t *full_module)
{
    (void)full_module;
    return -1;
}
int32_t pm_metal_wasm_load_register(const uint8_t *full_module, const uint8_t *bytes, uint32_t len)
{
    (void)full_module;
    (void)bytes;
    (void)len;
    return -1;
}
int32_t pm_metal_wasm_load_verified(const uint8_t *full_module, const uint8_t *bytes, uint32_t len,
                                    const uint8_t *sig, uint32_t sig_len)
{
    (void)full_module;
    (void)bytes;
    (void)len;
    (void)sig;
    (void)sig_len;
    return -1;
}
int32_t pm_metal_wasm_unload(const uint8_t *full_module)
{
    (void)full_module;
    return -1;
}
int32_t pm_metal_wasm_call0(const uint8_t *full_module, const uint8_t *func)
{
    (void)full_module;
    (void)func;
    return -1;
}
int32_t pm_metal_wasm_proof_stress(void) { return -1; }
int32_t pm_metal_wasm_proof(void) { return -1; }

int32_t pm_metal_inspect_py_app(void) { return 0; }
int32_t pm_metal_inspect_py_dispatch(void) { return 0; }
int32_t pm_metal_inspect_py_ready(void) { return 0; }

int32_t pm_metal_pack_names(void) { return 0; }
