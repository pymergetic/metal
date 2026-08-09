/* Host WAMR port — Alloc_With_Pool from Metal mem (one memory). */
#include "runtime.h"

#include <string.h>

#include <pymergetic/metal/mem/__init__.h>

#include "guest_coro.h"
#include "wasm_export.h"

/* Pool must fit large guests (multi-MiB wasm + linear memory). */
#define POOL_BYTES (32u * 1024u * 1024u)
#define STACK_BYTES (64u * 1024u)
#define HEAP_BYTES (256u * 1024u)
#define NAME_MAX 96

/* Host build has libc (mmap for W^X pages); freestanding (BIOS/EFI,
 * `-ffreestanding -nostdinc`) does not, and needs none: Metal sets up
 * no NX/execute-protection page tables on either freestanding target,
 * so ordinary `pm_metal_mem_alloc`'d memory is already executable
 * there. `BH_PLATFORM_METAL` is the same freestanding-vs-host define
 * build.rs already threads through for WAMR itself -- reused here
 * rather than inventing a second one. */
#ifndef BH_PLATFORM_METAL
#include <sys/mman.h>
#endif

/* First-argument register differs by ABI: SysV (Linux host, and the
 * freestanding `x86_64-unknown-none-elf` BIOS target, which is SysV
 * too) passes it in RDI; the freestanding UEFI target cross-compiles
 * as `x86_64-unknown-windows-gnu` (Windows x64 ABI), which passes it
 * in RCX instead. `PM_METAL_WASM_TRAMP_WIN64` is set by build.rs only
 * for that one target -- see `gen_trampolines`'s replacement,
 * `stamp_tramp` below. */
#ifdef PM_METAL_WASM_TRAMP_WIN64
#define TRAMP_MOVABS_OPCODE 0xb9 /* mov rcx, imm64 */
#else
#define TRAMP_MOVABS_OPCODE 0xbf /* mov rdi, imm64 */
#endif

/* Heap-allocated ring, not a fixed array -- one node per currently
 * loaded wasm pack, no cap on how many can be loaded at once (same
 * shape as `pymergetic_metal_reg`'s `KernelTable` raw ring: no
 * concurrent-access locking here either, matching this file's existing
 * single-caller assumption -- load/unload are never called concurrently
 * with each other or with a call in flight). */
typedef struct slot_s {
  char name[NAME_MAX];
  wasm_module_t module;
  wasm_module_inst_t inst;
  wasm_exec_env_t exec;
  uint8_t *buf; /* wasm_runtime_load's buffer: must outlive the module
                 * (WAMR may reference it internally until
                 * wasm_runtime_unload), so it is owned by the slot, not
                 * freed right after the load call. */
  uint32_t buf_len;
  struct slot_s *next;
} slot_t;

/* One `RegEntry`-publishable trampoline. `code` *is* the callable
 * `() -> i32` function -- its address is handed straight to the
 * registry, no wrapper indirection. `code` is stamped exactly once,
 * when the node is first allocated (see `stamp_tramp`); everything a
 * later claim/release cycle changes afterwards (`slot`, `func`) is
 * ordinary data, not code, so nothing here is ever self-modified after
 * its first (pre-execution) write -- no icache-coherency hazard.
 *
 * `all_next` is permanent (every node ever allocated, for
 * `free_slot`'s "release every tramp bound to this instance" sweep);
 * `free_next` only means something while `slot == NULL` (unclaimed,
 * ready to reuse). Growing the pool never happens by picking a bigger
 * constant -- `grow_tramp_arena` mints one more arena, forever, so
 * there is no cap for a build's own packages *or* for whatever a wasm
 * pack a different developer loads later declares. */
typedef struct tramp_s {
  uint8_t code[24]; /* mov r?x, imm64 (10B) + jmp [rip+0] (6B) + abs addr (8B) */
  slot_t *slot;     /* NULL when unclaimed/free */
  char func[64];
  struct tramp_s *all_next;
  struct tramp_s *free_next;
} tramp_t;

/* One arena allocation to free on shutdown (bookkeeping node itself is
 * ordinary, non-executable memory -- only `mem` needs to be RWX). */
typedef struct arena_s {
  void *mem;
  size_t bytes;
  struct arena_s *next;
} arena_t;

/* One dynamically-discovered cross-package import, registered as a
 * WAMR native the first time any loaded package's own "pm_metal_imports"
 * custom section names it (see `register_dynamic_imports`) -- never
 * from a forge-generated, kernel-compiled table. `module`/`func` and
 * `sym` all live in this one allocation, so `wasm_runtime_register_natives`
 * (which keeps the `module_name`/`native_symbols` pointers it is given
 * forever, no clone -- see external/wamr's `wasm_native.c`
 * `register_natives`: `node->module_name = module_name;
 * node->native_symbols = native_symbols;`) gets pointers that outlive
 * it without a second allocation. Deduped by `(module, func)` on
 * `g_fwd_regs` so importing the same pair from two different consumer
 * packages only ever registers it with WAMR once.
 *
 * `cached` is a resolve-once slot pointer, same pattern as this file's
 * own trampolines (`tramp_t.slot`) and the registry's `RegEntry`: the
 * first call after registration (or after invalidation) pays a real
 * `find_slot`; every call after that is a straight pointer read, no
 * per-call name lookup. `free_slot` sweeps `g_fwd_regs` exactly like it
 * already sweeps `g_tramp_all`, so a stale pointer into freed slot
 * memory never survives past the provider's own unload -- correctness,
 * not just speed, since a reused address could otherwise resolve to a
 * *different* module loaded later under a different name. */
typedef struct fwd_reg_s {
  char module[NAME_MAX];
  char func[64];
  NativeSymbol sym;
  slot_t *cached;
  struct fwd_reg_s *next;
} fwd_reg_t;

static int g_ready;
static uint8_t *g_pool;
static slot_t *g_slots_head;
static tramp_t *g_tramp_all;
static tramp_t *g_tramp_free;
static arena_t *g_tramp_arenas;
static fwd_reg_t *g_fwd_regs;

static int32_t tramp_dispatch(tramp_t *t);

#ifndef BH_PLATFORM_METAL
static void *exec_alloc(size_t n)
{
  void *p = mmap(NULL, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? NULL : p;
}
static void exec_free(void *p, size_t n)
{
  munmap(p, n);
}
#else
static void *exec_alloc(size_t n)
{
  return pm_metal_mem_alloc((uint32_t)n);
}
static void exec_free(void *p, size_t n)
{
  (void)n;
  pm_metal_mem_free((uint8_t *)p);
}
#endif

/* Write the one-time-forever machine code for `t`: load `t` itself
 * into the ABI's first argument register, then an absolute (never
 * range-limited, unlike a relative `jmp rel32`, which could exceed
 * +-2GiB under ASLR between an mmap'd page and the main binary) tail
 * jump into `tramp_dispatch`. `tramp_dispatch` reads `t->slot`/
 * `t->func` at call time, so restamping on reclaim is never needed --
 * only those two plain fields change across a claim/release cycle. */
static void stamp_tramp(tramp_t *t)
{
  uint64_t self_addr = (uint64_t)(uintptr_t)t;
  uint64_t target_addr = (uint64_t)(uintptr_t)tramp_dispatch;
  uint8_t *code = t->code;
  code[0] = 0x48; /* REX.W */
  code[1] = (uint8_t)TRAMP_MOVABS_OPCODE;
  memcpy(&code[2], &self_addr, 8);
  code[10] = 0xff;
  code[11] = 0x25;
  code[12] = 0x00;
  code[13] = 0x00;
  code[14] = 0x00;
  code[15] = 0x00;
  memcpy(&code[16], &target_addr, 8);
}

/* Mint one more arena of fresh, pre-stamped, unclaimed trampolines --
 * called only when the free list is empty, so the pool's real size is
 * "however many are claimed right now", never a fixed ceiling. */
static void grow_tramp_arena(void)
{
  size_t n, i;
  uint8_t *mem;
  arena_t *a;
  enum { ARENA_BYTES = 4096 };

  n = ARENA_BYTES / sizeof(tramp_t);
  if (n == 0) {
    n = 1;
  }
  mem = (uint8_t *)exec_alloc(n * sizeof(tramp_t));
  if (mem == NULL) {
    return;
  }
  a = (arena_t *)pm_metal_mem_alloc(sizeof(arena_t));
  if (a == NULL) {
    exec_free(mem, n * sizeof(tramp_t));
    return;
  }
  a->mem = mem;
  a->bytes = n * sizeof(tramp_t);
  a->next = g_tramp_arenas;
  g_tramp_arenas = a;

  for (i = 0; i < n; i++) {
    tramp_t *t = &((tramp_t *)mem)[i];
    t->slot = NULL;
    memset(t->func, 0, sizeof(t->func));
    stamp_tramp(t);
    t->all_next = g_tramp_all;
    g_tramp_all = t;
    t->free_next = g_tramp_free;
    g_tramp_free = t;
  }
}

static tramp_t *alloc_tramp(void)
{
  tramp_t *t;
  if (g_tramp_free == NULL) {
    grow_tramp_arena();
    if (g_tramp_free == NULL) {
      return NULL;
    }
  }
  t = g_tramp_free;
  g_tramp_free = t->free_next;
  return t;
}

static int cstr_copy(char *dst, size_t dst_n, const uint8_t *src)
{
  size_t i;
  if (src == NULL || dst_n == 0) {
    return -1;
  }
  for (i = 0; i + 1 < dst_n; i++) {
    dst[i] = (char)src[i];
    if (src[i] == 0) {
      return 0;
    }
  }
  dst[dst_n - 1] = 0;
  return -1;
}

static slot_t *find_slot(const char *name)
{
  slot_t *s;
  for (s = g_slots_head; s != NULL; s = s->next) {
    if (strcmp(s->name, name) == 0) {
      return s;
    }
  }
  return NULL;
}

static slot_t *alloc_slot(void)
{
  slot_t *s = (slot_t *)pm_metal_mem_alloc(sizeof(slot_t));
  if (s == NULL) {
    return NULL;
  }
  memset(s, 0, sizeof(*s));
  s->next = g_slots_head;
  g_slots_head = s;
  return s;
}

static void free_slot(slot_t *s)
{
  slot_t **pp;
  tramp_t *t;
  fwd_reg_t *r;
  if (s == NULL) {
    return;
  }
  /* Release every export trampoline this instance claimed -- back to
   * the free list for reuse, otherwise a reload cycle leaks a node per
   * claim, the exact "stale publish never withdrawn" gap the old
   * dynamic table had. Code is never rewritten -- only `slot` (cleared
   * here) and `func` are live data. */
  for (t = g_tramp_all; t != NULL; t = t->all_next) {
    if (t->slot == s) {
      t->slot = NULL;
      t->free_next = g_tramp_free;
      g_tramp_free = t;
    }
  }
  /* Same invalidation for every forwarding native's resolve-once cache
   * -- otherwise a later, unrelated slot allocation reusing this same
   * freed address would make `fwd_native` silently forward calls to
   * the wrong module. */
  for (r = g_fwd_regs; r != NULL; r = r->next) {
    if (r->cached == s) {
      r->cached = NULL;
    }
  }
  if (s->exec != NULL) {
    wasm_runtime_destroy_exec_env(s->exec);
  }
  if (s->inst != NULL) {
    wasm_runtime_deinstantiate(s->inst);
  }
  if (s->module != NULL) {
    wasm_runtime_unload(s->module);
  }
  if (s->buf != NULL) {
    pm_metal_mem_free(s->buf);
  }
  for (pp = &g_slots_head; *pp != NULL; pp = &(*pp)->next) {
    if (*pp == s) {
      *pp = s->next;
      break;
    }
  }
  pm_metal_mem_free((uint8_t *)s);
}

static int32_t call_export(slot_t *s, const char *func)
{
  wasm_function_inst_t f;
  uint32_t argv[1];

  if (s == NULL || func == NULL || s->inst == NULL || s->exec == NULL) {
    return -1;
  }
  f = wasm_runtime_lookup_function(s->inst, func);
  if (f == NULL) {
    return -1;
  }
  argv[0] = 0;
  if (!wasm_runtime_call_wasm(s->exec, f, 0, argv)) {
    return -1;
  }
  return (int32_t)argv[0];
}

/* The callee every stamped trampoline's raw machine code tail-jumps
 * into (see `stamp_tramp`) -- internal-linkage only, never exposed to
 * Rust: the registry-publishable address is `t->code` itself, not this
 * function, so there is nothing left for a Rust-side pool to resolve
 * ("N distinct callable addresses") the way `trampolines::ptr` used
 * to. */
static int32_t tramp_dispatch(tramp_t *t)
{
  return call_export(t->slot, t->func);
}

/* The one shared native every dynamically-discovered cross-package
 * import resolves to (see `register_dynamic_imports`) -- unlike the
 * registry-publish trampolines above, this never needs hand-stamped
 * machine code: WAMR already carries a per-registration `attachment`
 * cookie through to the native call (`wasm_runtime_get_function_attachment`,
 * a public wasm_export.h API), so one ordinary C function serves every
 * distinct `(module, func)` pair, keyed by whichever `fwd_reg_t` this
 * particular import was registered with.
 *
 * `r->cached` is a resolve-once slot pointer (`find_slot`'s linked-list
 * walk only runs on the first call, or the first call after
 * invalidation) -- `free_slot` clears it the moment the target
 * unloads, so this stays correct across the provider's own
 * unload/reload cycles the same way the uncached path always was,
 * degrading to -1 whenever the name currently resolves to nothing
 * (before the provider ever loaded, or after it unloaded and hasn't
 * reloaded yet). */
static int32_t fwd_native(wasm_exec_env_t exec_env)
{
  fwd_reg_t *r = (fwd_reg_t *)wasm_runtime_get_function_attachment(exec_env);
  if (r == NULL) {
    return -1;
  }
  if (r->cached == NULL) {
    r->cached = find_slot(r->module);
  }
  return call_export(r->cached, r->func);
}

/* Register one `(import_module, func)` forwarding native with WAMR, if
 * not already registered (WAMR's own native-symbol table is a simple
 * linked list keyed by module name, appended to by every call --
 * registering the same pair twice would just waste a node, not break
 * anything, but `g_fwd_regs` dedupes anyway since this can be reached
 * once per consumer package that happens to import the same pair). */
static void register_one_import(const char *module, const char *func)
{
  fwd_reg_t *r;
  /* Kernel modules are host natives (host_natives.c), not wasm fwd. */
  if (module != NULL && strncmp(module, "pymergetic.metal.", 17) == 0) {
    return;
  }
  for (r = g_fwd_regs; r != NULL; r = r->next) {
    if (strcmp(r->module, module) == 0 && strcmp(r->func, func) == 0) {
      return;
    }
  }
  r = (fwd_reg_t *)pm_metal_mem_alloc(sizeof(fwd_reg_t));
  if (r == NULL) {
    return;
  }
  memset(r, 0, sizeof(*r));
  if (cstr_copy(r->module, sizeof(r->module), (const uint8_t *)module) != 0
      || cstr_copy(r->func, sizeof(r->func), (const uint8_t *)func) != 0) {
    pm_metal_mem_free((uint8_t *)r);
    return;
  }
  r->sym.symbol = r->func;
  r->sym.func_ptr = (void *)fwd_native;
  r->sym.signature = "()i";
  r->sym.attachment = r;
  if (!wasm_runtime_register_natives(r->module, &r->sym, 1)) {
    pm_metal_mem_free((uint8_t *)r);
    return;
  }
  r->next = g_fwd_regs;
  g_fwd_regs = r;
}

/* Read one ULEB128 varint from `buf[*pos..limit)`, advancing `*pos`
 * past it (wasm's own integer encoding -- used only for the section
 * framing bytes the spec mandates; the payload after the section name
 * is this file's own plain little-endian format, see below). */
static uint32_t read_uleb32(const uint8_t *buf, uint32_t limit, uint32_t *pos)
{
  uint32_t result = 0, shift = 0;
  while (*pos < limit) {
    uint8_t b = buf[(*pos)++];
    result |= (uint32_t)(b & 0x7f) << shift;
    if ((b & 0x80) == 0) {
      break;
    }
    shift += 7;
  }
  return result;
}

#define PM_METAL_IMPORTS_SECTION "pm_metal_imports"

/* Scan a raw wasm binary for the custom section forge's `_wasm_import_section`
 * writer embeds (see docs/definitions/module.md "Cross-package imports")
 * and register a forwarding native for every `(module, func)` pair it
 * lists -- called once per load, before `wasm_runtime_instantiate`
 * resolves *this* module's own imports. Reads straight from the bytes
 * this call was handed, not any build-time table, so it works
 * identically whether `bytes` was compiled in at kernel-build time or
 * fetched from disk/network after boot. A malformed or absent section
 * is silently a no-op (this module simply declares no cross-package
 * imports as far as the host can tell). */
/* Read one `"module\0func\0"` pair starting at `*pos` (bumping `*pos`
 * past both strings) and register it. Bails (returns -1) without
 * registering anything if either string runs off the end of `[0,
 * limit)` without a NUL -- malformed payload, not this reader's job to
 * guess at. */
static int read_and_register_pair(const uint8_t *bytes, uint32_t limit, uint32_t *pos)
{
  const char *module;
  const char *func;
  uint32_t mlen, flen;

  module = (const char *)&bytes[*pos];
  mlen = 0;
  while (*pos + mlen < limit && bytes[*pos + mlen] != 0) {
    mlen++;
  }
  if (*pos + mlen >= limit) {
    return -1;
  }
  *pos += mlen + 1;

  func = (const char *)&bytes[*pos];
  flen = 0;
  while (*pos + flen < limit && bytes[*pos + flen] != 0) {
    flen++;
  }
  if (*pos + flen >= limit) {
    return -1;
  }
  *pos += flen + 1;

  register_one_import(module, func);
  return 0;
}

/* Register every `(module, func)` pair out of one already-located
 * `pm_metal_imports` section payload (`bytes[pos..sec_end)`, `pos`
 * already past the section name): a `u32` little-endian count, then
 * that many NUL-terminated string pairs (this file's own private
 * format -- see `_wasm_import_section.rs`'s writer). */
static void register_imports_payload(const uint8_t *bytes, uint32_t pos, uint32_t sec_end)
{
  uint32_t count, i;

  if (pos + 4 > sec_end) {
    return;
  }
  count = (uint32_t)bytes[pos] | ((uint32_t)bytes[pos + 1] << 8) | ((uint32_t)bytes[pos + 2] << 16)
          | ((uint32_t)bytes[pos + 3] << 24);
  pos += 4;
  for (i = 0; i < count && pos < sec_end; i++) {
    if (read_and_register_pair(bytes, sec_end, &pos) != 0) {
      return;
    }
  }
}

/* Scan a raw wasm binary for the custom section forge's
 * `_wasm_import_section` writer embeds (see
 * docs/definitions/module.md "Cross-package imports") and register a
 * forwarding native for every `(module, func)` pair it lists -- called
 * once per load, before `wasm_runtime_instantiate` resolves *this*
 * module's own imports. Reads straight from the bytes this call was
 * handed, not any build-time table, so it works identically whether
 * `bytes` was compiled in at kernel-build time or fetched from
 * disk/network after boot. A malformed or absent section is silently a
 * no-op (this module simply declares no cross-package imports as far
 * as the host can tell). */
static void register_dynamic_imports(const uint8_t *bytes, uint32_t len)
{
  uint32_t pos;
  const uint32_t name_len = sizeof(PM_METAL_IMPORTS_SECTION) - 1;

  if (bytes == NULL || len < 8) {
    return;
  }
  pos = 8; /* skip \0asm magic (4B) + version (4B) */
  while (pos < len) {
    uint8_t id;
    uint32_t size, sec_start, sec_end, name_pos, this_name_len;

    id = bytes[pos++];
    size = read_uleb32(bytes, len, &pos);
    sec_start = pos;
    if (size > len - sec_start) {
      return; /* malformed -- bail rather than read past len */
    }
    sec_end = sec_start + size;

    if (id == 0) {
      name_pos = sec_start;
      this_name_len = read_uleb32(bytes, sec_end, &name_pos);
      if (this_name_len == name_len && name_pos + this_name_len <= sec_end
          && memcmp(&bytes[name_pos], PM_METAL_IMPORTS_SECTION, name_len) == 0) {
        register_imports_payload(bytes, name_pos + this_name_len, sec_end);
      }
    }
    pos = sec_end;
  }
}

int32_t pm_metal_wasm_port_ready(void)
{
  return g_ready ? 1 : 0;
}

uint32_t pm_metal_wasm_port_guest_coro_create(const uint8_t *full_module, uint32_t state_bytes)
{
  char name[NAME_MAX];
  slot_t *s;

  if (!g_ready || full_module == NULL) {
    return 0u;
  }
  if (cstr_copy(name, sizeof(name), full_module) != 0) {
    return 0u;
  }
  s = find_slot(name);
  if (s == NULL || s->inst == NULL) {
    return 0u;
  }
  return pm_metal_wasm_guest_coro_create_inst(s->inst, state_bytes);
}

int32_t pm_metal_wasm_port_init(void)
{
  RuntimeInitArgs args;

  if (g_ready) {
    return 0;
  }
  g_pool = pm_metal_mem_alloc(POOL_BYTES);
  if (g_pool == NULL) {
    return -1;
  }
  memset(&args, 0, sizeof(args));
  args.mem_alloc_type = Alloc_With_Pool;
  args.mem_alloc_option.pool.heap_buf = g_pool;
  args.mem_alloc_option.pool.heap_size = (unsigned)POOL_BYTES;
  if (!wasm_runtime_full_init(&args)) {
    pm_metal_mem_free(g_pool);
    g_pool = NULL;
    return -1;
  }
  /* Kernel guest_surface imports (log, …) — before any pack instantiate. */
  if (pm_metal_wasm_port_register_host_natives() != 0) {
    wasm_runtime_destroy();
    pm_metal_mem_free(g_pool);
    g_pool = NULL;
    return -1;
  }
  g_ready = 1;
  return 0;
}

void pm_metal_wasm_port_shutdown(void)
{
  if (!g_ready) {
    return;
  }
  while (g_slots_head != NULL) {
    free_slot(g_slots_head);
  }
  g_tramp_all = NULL;
  g_tramp_free = NULL;
  while (g_tramp_arenas != NULL) {
    arena_t *a = g_tramp_arenas;
    g_tramp_arenas = a->next;
    exec_free(a->mem, a->bytes);
    pm_metal_mem_free((uint8_t *)a);
  }
  /* wasm_runtime_destroy() below frees WAMR's own NativeSymbolsNode
   * wrappers but not the module_name/native_symbols memory we handed
   * it (see fwd_reg_t's doc comment) -- and forgets the registrations
   * themselves, so `g_fwd_regs`'s dedup tracking would otherwise be
   * stale (claiming "already registered") after a re-init. Free both
   * together here. */
  while (g_fwd_regs != NULL) {
    fwd_reg_t *r = g_fwd_regs;
    g_fwd_regs = r->next;
    pm_metal_mem_free((uint8_t *)r);
  }
  wasm_runtime_destroy();
  if (g_pool != NULL) {
    pm_metal_mem_free(g_pool);
    g_pool = NULL;
  }
  g_ready = 0;
}

int32_t pm_metal_wasm_port_load(const uint8_t *full_module, const uint8_t *bytes, uint32_t len)
{
  char name[NAME_MAX];
  char err[128];
  slot_t *s;

  if (!g_ready || full_module == NULL || bytes == NULL || len == 0) {
    return -1;
  }
  if (cstr_copy(name, sizeof(name), full_module) != 0) {
    return -1;
  }
  /* Must happen before wasm_runtime_instantiate (below) resolves this
   * module's own imports -- registers a forwarding native for every
   * cross-package import this module's own "pm_metal_imports" custom
   * section declares, discovered from these bytes alone. */
  register_dynamic_imports(bytes, len);
  /* Fresh heap node every load, even for a reload of the same name --
   * `free_slot` fully frees the old one (unlike the old fixed-array
   * version, which reused the same static slot in place), so any prior
   * node must go before allocating its replacement. */
  s = find_slot(name);
  if (s != NULL) {
    free_slot(s);
  }
  s = alloc_slot();
  if (s == NULL) {
    return -1;
  }

  /* WAMR may mutate this buffer for its own footprint/performance
   * purposes and requires it stay referenceable until
   * wasm_runtime_unload -- not just for the duration of this call (see
   * wasm_export.h's wasm_runtime_load doc). Owned by the slot, freed by
   * free_slot alongside the module it belongs to. */
  s->buf = pm_metal_mem_alloc(len);
  if (s->buf == NULL) {
    free_slot(s);
    return -1;
  }
  memcpy(s->buf, bytes, len);
  s->buf_len = len;
  memset(err, 0, sizeof(err));
  s->module = wasm_runtime_load(s->buf, len, err, (uint32_t)sizeof(err));
  if (s->module == NULL) {
    free_slot(s);
    return -1;
  }
  s->inst = wasm_runtime_instantiate(s->module, STACK_BYTES, HEAP_BYTES, err, (uint32_t)sizeof(err));
  if (s->inst == NULL) {
    free_slot(s);
    return -1;
  }
  s->exec = wasm_runtime_create_exec_env(s->inst, STACK_BYTES);
  if (s->exec == NULL) {
    free_slot(s);
    return -1;
  }
  memcpy(s->name, name, sizeof(s->name));
  return 0;
}

void pm_metal_wasm_port_unload(const uint8_t *full_module)
{
  char name[NAME_MAX];
  slot_t *s;
  if (full_module == NULL || cstr_copy(name, sizeof(name), full_module) != 0) {
    return;
  }
  s = find_slot(name);
  free_slot(s);
}

int32_t pm_metal_wasm_port_call0(const uint8_t *full_module, const uint8_t *func)
{
  char name[NAME_MAX];
  char fname[64];
  slot_t *s;
  if (!g_ready || full_module == NULL || func == NULL) {
    return -1;
  }
  if (cstr_copy(name, sizeof(name), full_module) != 0) {
    return -1;
  }
  if (cstr_copy(fname, sizeof(fname), func) != 0) {
    return -1;
  }
  s = find_slot(name);
  return call_export(s, fname);
}

/* Only () -> i32 exports are publishable through a fixed-signature
 * RegEntry/`call_export` trampoline -- see `fwd_native` for the (also
 * 0-arg, i32-return) guest-to-guest path, the same constraint applies
 * there for the same reason. */
static int is_i32_niladic_export(const wasm_export_t *ex)
{
  if (ex->kind != WASM_IMPORT_EXPORT_KIND_FUNC || ex->name == NULL) {
    return 0;
  }
  if (wasm_func_type_get_param_count(ex->u.func_type) != 0
      || wasm_func_type_get_result_count(ex->u.func_type) != 1) {
    return 0;
  }
  return wasm_func_type_get_result_valkind(ex->u.func_type, 0) == WASM_I32;
}

/* How many `() -> i32` exports `full_module` has -- the registry-publish
 * caller (Rust `wasm::register`) sizes its `RegEntry` array from this,
 * then calls `pm_metal_wasm_port_export_name`/`_claim_trampoline` once
 * per index `0..count`. */
int32_t pm_metal_wasm_port_export_count(const uint8_t *full_module)
{
  char name[NAME_MAX];
  slot_t *s;
  int32_t nexp, i, n;
  if (!g_ready || full_module == NULL || cstr_copy(name, sizeof(name), full_module) != 0) {
    return -1;
  }
  s = find_slot(name);
  if (s == NULL || s->module == NULL) {
    return -1;
  }
  nexp = wasm_runtime_get_export_count(s->module);
  if (nexp < 0) {
    return -1;
  }
  n = 0;
  for (i = 0; i < nexp; i++) {
    wasm_export_t ex;
    memset(&ex, 0, sizeof(ex));
    wasm_runtime_get_export_type(s->module, i, &ex);
    if (is_i32_niladic_export(&ex)) {
      n++;
    }
  }
  return n;
}

/* Write the `idx`-th `() -> i32` export's name (NUL-terminated) into
 * `buf`. `idx` indexes only the filtered (publishable) subset, matching
 * `pm_metal_wasm_port_export_count`'s count and `_claim_trampoline`'s
 * `idx`. */
int32_t pm_metal_wasm_port_export_name(const uint8_t *full_module, int32_t idx, uint8_t *buf,
                                        uint32_t buf_n)
{
  char name[NAME_MAX];
  slot_t *s;
  int32_t nexp, i, n;
  if (!g_ready || full_module == NULL || buf == NULL || idx < 0
      || cstr_copy(name, sizeof(name), full_module) != 0) {
    return -1;
  }
  s = find_slot(name);
  if (s == NULL || s->module == NULL) {
    return -1;
  }
  nexp = wasm_runtime_get_export_count(s->module);
  n = 0;
  for (i = 0; i < nexp; i++) {
    wasm_export_t ex;
    memset(&ex, 0, sizeof(ex));
    wasm_runtime_get_export_type(s->module, i, &ex);
    if (!is_i32_niladic_export(&ex)) {
      continue;
    }
    if (n == idx) {
      size_t len = strlen(ex.name);
      if (len + 1 > buf_n) {
        return -1;
      }
      memcpy(buf, ex.name, len + 1);
      return 0;
    }
    n++;
  }
  return -1;
}

/* Claim a trampoline bound to `(full_module, func)` and return its
 * `RegEntry`-publishable address directly (`t->code`, already a real
 * `() -> i32` function) -- NULL if `func` is not a publishable export
 * or the arena couldn't grow. No count anywhere: `alloc_tramp` reuses
 * a released node or mints a fresh arena, so this never runs out
 * because of how many exports *this build's* packages happened to
 * have -- it also has to work for a wasm pack some other developer
 * loads later, which forge cannot see the export count of ahead of
 * time. Released automatically by `free_slot` on unload -- never freed
 * one at a time. */
void *pm_metal_wasm_port_claim_trampoline(const uint8_t *full_module, const uint8_t *func)
{
  char name[NAME_MAX];
  char fname[64];
  slot_t *s;
  tramp_t *t;
  if (!g_ready || full_module == NULL || func == NULL
      || cstr_copy(name, sizeof(name), full_module) != 0
      || cstr_copy(fname, sizeof(fname), func) != 0) {
    return NULL;
  }
  s = find_slot(name);
  if (s == NULL) {
    return NULL;
  }
  t = alloc_tramp();
  if (t == NULL) {
    return NULL;
  }
  t->slot = s;
  memcpy(t->func, fname, sizeof(t->func));
  return (void *)t->code;
}

int32_t pm_metal_wasm_port_image(const uint8_t *full_module, const uint8_t **out_bytes,
                                 uint32_t *out_len)
{
  char name[NAME_MAX];
  slot_t *s;
  if (out_bytes == NULL || out_len == NULL || full_module == NULL
      || cstr_copy(name, sizeof(name), full_module) != 0) {
    return -1;
  }
  s = find_slot(name);
  if (s == NULL || s->buf == NULL || s->buf_len == 0) {
    return -1;
  }
  *out_bytes = s->buf;
  *out_len = s->buf_len;
  return 0;
}
