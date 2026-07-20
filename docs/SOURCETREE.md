# Source tree

Maps to [LAYERS.md](LAYERS.md). Stops at wasm interface.

**Active target:** `efi` only. Freestanding Metal modules live under
`src/pymergetic/metal/` (contracts, `pm_metal_<module>_*`) with EFI bodies in
`src/efi/pymergetic/metal/` and EDK2 entry in `src/efi/MetalPkg/`. Hosted
linux/zephyr/nuttx trees and the old `src/common/…` host stack are on
`archive/multi-host-linux-zephyr-nuttx`. Examples below may still describe that
archived layout.

---

## Two trees (plus one narrow exception)

| Tree | Who compiles against it |
|------|-------------------------|
| `include/pymergetic/metal/` | **mods** (guest API) |
| `src/` | **runtime binary** (everything else) |

Mods use **wasi-sdk sysroot** + `-I include/`. Start with `#include <pymergetic/metal/metal.h>`.

**Exception — dual WASI-import headers (`util/`, `net/`, and EFI `gfx`/`ui`/`shell`):** small utilities (`util/size.h`, `util/arena.h`, `util/log.h`, `util/lz4.h`, `util/tar.h`, `util/crypto.h`) and the Metal networking surface (`net/addr.h`, `net/dns.h`, `net/udp.h`, `net/tcp.h`, `net/ntp.h`, `net/http.h`) are genuinely useful on both sides, but unlike everything else here there is only **one** implementation of them, ever — `src/common/pymergetic/metal/util/{size,arena,log,lz4,tar,crypto}.c` and `src/common/pymergetic/metal/net/{dns,udp,tcp,ntp,http}.c`, `impl: common`, linked into the runtime binary same as any other common module. A mod including one of these headers gets a *different* declaration on wasm32 than the runtime does on its native target: instead of an ordinary prototype backed by that mod's own compiled object code, it gets a real wasm import (`PM_METAL_WASI_IMPORT(module, name)` from `metal/wasi.h`, expanding to `__attribute__((import_module(module), import_name(#name)))`, guarded by `#if defined(__wasm__)` in the header itself) with **no local body at all** — resolved against that same `.c` file's own `wasm_runtime_register_natives()` call (each module registers its own small `NativeSymbol` table, called once from `runtime.c`'s `init()` — no shared central bridge file; WAMR's registration list is a plain linked list keyed by module-name string, so N independent calls under different names coexist without conflict) at `wasm_runtime_instantiate()` time, exactly like a real WASI import. `metal/wasi.h` only unifies the attribute *shape* — each header still picks its own `import_module` name (`PM_METAL_UTIL_ARENA_WASI_MODULE` = `"pymergetic.metal.util.arena"`, `PM_METAL_NET_DNS_WASI_MODULE` = `"pymergetic.metal.net.dns"`, …), one per module rather than one shared name, so none of these imports can ever collide with anything else, including each other; not the Emscripten-popularized `"env"` convention either way. Each name is a plain `#define`d string constant that both the guest-side attribute and that module's own host-side registration call build from, so they can never drift apart into two different strings. This is DRY in the stronger sense — one compiled implementation, not two copies of the same logic built for two targets — at the cost of a wasm import call replacing what would otherwise be a local function call. `net/{dns,udp,tcp,ntp,http}` sits **alongside** WASI sockets as a second Metal net API (socket fds opaque to WASI fd space). Pointer parameters/returns are always addresses in the *calling* module's own linear memory — WAMR auto-translates each at the import boundary.

On the active **efi** target the same dual-header pattern applies to product surface APIs under `include/pymergetic/metal/{gfx,ui,shell,async,input,fs}.h` (`pymergetic.metal.gfx` / `.ui` / `.shell` / `.async` / `.input` / `.fs`): guests import them; EFI bodies + `*_native_register()` live in `src/efi/pymergetic/metal/{gfx,ui,shell,async,input,fs}/`. **UI/shell/async/input guest ABIs are handle-based** (no host pointers across the wasm boundary). Host-only helpers (`init`/`poll`/`ready`, session pump, ESP root, raw widget pointers) stay `#if !__wasm__`. ESP packages (e.g. `mods/apps/doom/{doom.wasm,doom1.wad}`) are staged beside `BOOTX64.EFI`; doom reads the IWAD via `metal.fs` into wasi-libc heap (WASI preopen `/` is available for `fopen` probes). Everything else in `include/` remains **mod-facing only** — the runtime must not include from there (except these dual-header modules).

---

## Naming

### Files

**Rule:** `foo.h` ↔ `foo.c` same basename. A module may have **multiple** `foo.c` (common + per-plat) — linker merges; each function lives in exactly one `.c`.

**Order:** definitions in every `foo.c` follow the **same order** as declarations in `foo.h`. Skip symbols a given `.c` does not implement; do not reorder.

**Placeholders:** for each skipped symbol, leave a comment in that slot — same order, no code:

```c
/* not impl: bind — src/linux/pymergetic/metal/port/platform.c */
/* not impl: bind — src/zephyr/pymergetic/metal/port/platform.c */
```

Reason optional (`WAMR provides`, `plat-only`, …). Makes gaps grep-able and reviews obvious.

| Tree | Path |
|------|------|
| mod-facing | `include/pymergetic/metal/…/foo.h` |
| common | `src/common/pymergetic/metal/…/foo.h` · `foo.c` |
| per-plat | `src/<plat>/pymergetic/metal/…/foo.c` |
| plat-private | `src/<plat>/pymergetic/metal/…/foo.h` · `foo.c` (no common header) |

Exceptions: `main.c`, `mods/tests/*/main.c`.

### Impl sites (per function)

Every declaration in a contract header tags where the body lives:

| Tag | Body in | Rule |
|-----|---------|------|
| `/* impl: common */` | `src/common/…/foo.c` | one copy, all targets link it |
| `/* impl: bind */` | `src/<plat>/…/foo.c` | **every** built target has an impl |
| `/* impl: zephyr */` | `src/zephyr/…/foo.c` | that target only (plat-private header) |
| `/* impl: wasi import */` | `src/common/…/util/{size,arena,log,lz4,tar,crypto}.c` + `src/common/…/net/{dns,udp,tcp,ntp,http}.c` + `src/common/…/mount/mount.c` — each registers its own `NativeSymbol` table, no shared bridge file | mod-facing `util/*.h` / `net/*.h` / `mount/mount.h` — one host impl, guests get a real wasm import, no local body at all (see "Two trees" above) |

One header can mix tags. Example `platform.h`: some calls OS-neutral → `common`; probes → `bind` on each plat.

```c
/* src/common/pymergetic/metal/port/platform.h */

/* impl: common */
pm_metal_port_target_id_t pm_metal_port_target_id(void);

/* impl: bind */
int pm_metal_port_read_file(const char *host_path, uint8_t **out_buf, uint32_t *out_len);
```

Symmetric naming lets you find `platform.c` in common and/or `src/linux/`, `src/zephyr/` and know which file owns which symbol.

**Ops-struct flavor of `bind`:** `pymergetic/metal/memory/` (see Tree below) groups closely-related `bind` functions into one struct-of-function-pointers per module instead of tagging each function separately. All three memory modules (`ram`, `kheap`, `bytecode`) share **one struct layout**, `pm_metal_memory_ops_t` in `memory/ops.h` — that header holds *only* the struct definition, nothing else. Each module then gets its own contract header declaring its own `bind` getter that returns a pointer to that shared struct type, with only the slots it uses filled in (the rest `NULL`):

```c
/* src/common/pymergetic/metal/memory/ops.h — the one shared layout,
 * plus a kind enum + resolve() for dynamic lookup (see below) */

typedef enum pm_metal_memory_kind {
	PM_METAL_MEMORY_RAM = 0,
	PM_METAL_MEMORY_KHEAP,
	PM_METAL_MEMORY_BYTECODE,
	PM_METAL_MEMORY_KIND_COUNT,
} pm_metal_memory_kind_t;

typedef struct pm_metal_memory_ops {
	uint64_t (*probe)(void);
	void *(*establish)(uint64_t requested_bytes, uint64_t *out_bytes);
	void (*release)(void);
	uint64_t (*bytes)(void);
	void *(*alloc)(uint32_t size);
	void (*free)(void *ptr);
} pm_metal_memory_ops_t;

/* impl: common — src/common/pymergetic/metal/memory/ops.c */
const pm_metal_memory_ops_t *pm_metal_memory_resolve(pm_metal_memory_kind_t kind);
```

```c
/* src/common/pymergetic/metal/memory/kheap.h — one getter, this module's contract */

#include "pymergetic/metal/memory/ops.h"

/* impl: bind — src/linux/…/memory/kheap.c
 *              src/zephyr/…/memory/kheap.c
 *
 * ->establish()/->release()/->bytes() are set; ->probe()/->alloc()/->free()
 * are NULL — this kind has no probe and is never sub-allocated. */
const pm_metal_memory_ops_t *pm_metal_memory_kheap_ops(void);
```

Each target's `.c` (one per module — `memory/kheap.c`, not a shared per-target `memory/ops.c`) defines one `static const` ops table (function pointers to `static` functions in that same file, `NULL` for the slots this module doesn't use) and the getter just returns its address — bound at build/link time like any other `bind` symbol, so there is no runtime registration step and the returned pointer is valid for the whole process lifetime. Callers do `pm_metal_memory_kheap_ops()->establish(...)`. `NULL` here always means "this module doesn't have this operation" (e.g. `ram` has no `alloc`) — a slot a module *does* use but a target hasn't implemented yet (e.g. zephyr's `kheap`/`bytecode` today) still gets a real stub function that returns `0`/`NULL` at call time, never a `NULL` field, so callers never need to null-check before calling a slot their module is documented to support. Use this flavor when a handful of functions are always used together and always come from the same target implementation (so grouping them behind one lookup is more useful than N separate symbols); use plain per-function `bind` (like `read_file` above) when a symbol stands alone.

`pm_metal_memory_ops.h`'s `pm_metal_memory_resolve(kind)` is a companion lookup for callers that pick a kind dynamically (e.g. a diagnostics loop over all three) instead of calling a dedicated getter at a call site that already knows its kind at compile time. It has exactly one implementation, `src/common/pymergetic/metal/memory/ops.c` (`impl: common`, not per-target) — it just `switch`es on `kind` and forwards to `pm_metal_memory_ram_ops()`/`kheap_ops()`/`bytecode_ops()`, so it carries no target-specific logic of its own.

### Symbols

```
pymergetic/metal/<module>/…/<stem>.h  →  pm_metal_<module>_…_<stem>_
```

- omit `<stem>` when it repeats the last dir (`runtime/runtime.h` → `pm_metal_runtime_`)
- sole contract in a module dir (`port/platform.h`) → `pm_metal_port_`

| Header | Prefix | Example |
|--------|--------|---------|
| `metal/metal.h` | — | umbrella only |
| `port/platform.h` | `pm_metal_port_` | `pm_metal_port_read_file()` |
| `runtime/runtime.h` | `pm_metal_runtime_` | `pm_metal_runtime_run_wasm()` |
| `memory/kheap.h` | `pm_metal_memory_kheap_` | `pm_metal_memory_kheap_ops()->establish()` |
| `runtime/process.h` | `pm_metal_process_` | `pm_metal_process_spawn()` |
| `app/app.h` | `pm_metal_app_` | `pm_metal_app_run_scripted()` |
| `util/log.h` | `pm_metal_util_log_` | `pm_metal_util_log_write()` |
| `util/lz4.h` | `pm_metal_util_lz4_` | `pm_metal_util_lz4_compress()` |
| `util/tar.h` | `pm_metal_util_tar_` | `pm_metal_util_tar_iter_next()` |
| `util/crypto.h` | `pm_metal_util_crypto_` | `pm_metal_util_crypto_hash()` |
| `net/ntp.h` | `pm_metal_net_ntp_` | `pm_metal_net_ntp_sync()` |
| `net/http.h` | `pm_metal_net_http_` | `pm_metal_net_http_get()` |

Private `src/<plat>/` symbols: `static` or plat-local.

---

## Tree

```
packages/metal/
│
├── include/pymergetic/metal/
│   ├── metal.h
│   ├── build.h                    # PM_METAL_BUILD_KERNEL — Visibility contract home
│   ├── mount/
│   │   └── mount.h                # privileged mount()/umount() — wasi import on wasm32 (+ KERNEL); bridge in src/…/mount/mount.c
│   ├── wasi.h                     # PM_METAL_WASI_IMPORT() — shared wasi-import attribute
│   ├── net/
│   │   ├── addr.h                 # portable IP address (types only)
│   │   ├── dns.h                  # contract — wasi import; DNS → port/dns
│   │   ├── udp.h                  # contract — wasi import; UDP → port/udp
│   │   ├── tcp.h                  # contract — wasi import; TCP → port/tcp
│   │   ├── ntp.h                  # contract — wasi import; SNTP over net/{dns,udp}
│   │   ├── tls.h                  # host TLS trust (inline → port/tls); CA for http
│   │   └── http.h                 # contract — wasi import; HTTP(S) → curl
│   └── util/
│       ├── size.h                 # contract — wasi import on wasm32, else src/common/…/size.c
│       ├── arena.h                # contract — wasi import on wasm32, else src/common/…/arena.c
│       ├── log.h                  # contract — wasi import on wasm32, else src/common/…/log.c
│       ├── lz4.h                  # contract — wasi import on wasm32, else src/common/…/lz4.c (thin wrapper over external/lz4)
│       ├── tar.h                  # contract — wasi import on wasm32, else src/common/…/tar.c (thin wrapper over external/microtar)
│       └── crypto.h               # contract — wasi import; Monocypher (external/monocypher)
│
├── src/
│   ├── common/pymergetic/metal/   # cross-target — runtime + contracts
│   │   ├── port/platform.h        # OS floor API (impl in src/<plat>/)
│   │   ├── port/lock.h            # one mutex primitive (impl in src/<plat>/) — see docs/RUNTIME.md "Concurrency"
│   │   ├── port/worker.h          # one background-thread primitive (impl in src/<plat>/)
│   │   ├── port/pipe.h            # one host pipe primitive (impl in src/<plat>/) — see docs/RUNTIME.md "Processes" > "Pipes"
│   │   ├── port/dns.h             # DNS resolve OS floor (impl in src/<plat>/…/dns.c)
│   │   ├── port/udp.h             # UDP OS floor (impl in src/<plat>/…/udp.c)
│   │   ├── port/tcp.h             # TCP OS floor (impl in src/<plat>/…/tcp.c)
│   │   ├── port/tls.h             # TLS trust-store OS floor (impl in src/<plat>/…/tls.c)
│   │   ├── net/dns.c              # DNS wasi bridge (host: inline → port/dns)
│   │   ├── net/udp.c              # UDP wasi bridge (host: inline → port/udp)
│   │   ├── net/tcp.c              # TCP wasi bridge (host: inline → port/tcp)
│   │   ├── net/ntp.c              # SNTP over net/{dns,udp} + wasi bridge
│   │   ├── net/http.c             # HTTP(S) transport (curl) + wasi bridge; CA via net/tls
│   │   ├── memory/                # ops-struct contracts (impl in src/<plat>/)
│   │   │   ├── memory.h           # convenience umbrella — re-exports the 4 below
│   │   │   ├── ops.h              # shared struct layout + kind enum + resolve()
│   │   │   ├── ops.c              # resolve() impl — impl: common, dispatches only
│   │   │   ├── ram.h              # machine RAM probe
│   │   │   ├── kheap.h            # WAMR pool (wasm linear mem + WAMR structs)
│   │   │   └── bytecode.h         # mod bytecode arena — separate from kheap
│   │   ├── mount/                 # mount table — see docs/MOUNT.md (linux feature-complete through 6c; zephyr blocked on wasi/file.c)
│   │   │   ├── ops.h              # shared ops-struct layout + kind enum + resolve()/kind_by_name()
│   │   │   ├── ops.c              # impl: common, dispatches only (mirrors memory/ops.c)
│   │   │   ├── hostdir.h / hostdir.c  # HOSTDIR fstype — impl: common (no per-target logic needed)
│   │   │   ├── tmpfs.h            # TMPFS fstype — shared ops-struct decl only, impl: bind (one .c per target)
│   │   │   ├── tmpfs_registry.h / tmpfs_registry.c  # name -> host-path + refcount bookkeeping — impl: common
│   │   │   ├── populate.h / populate.c  # ustar [+ lz4] blob registry + populate_all() extract against / — impl: common
│   │   │   ├── table.h / table.c  # host-only mount table — impl: common (resolve_ex / find_by_host for live remount)
│   │   │   ├── proc.h / proc.c    # virtual proc fstype + hook registry — impl: common
│   │   │   ├── proc/              # per-node generators + guest TLS (cmdline/environ via /proc/self)
│   │   │   ├── mount.c            # include/…/mount/mount.h's *only* impl + wasi native_register — same shape as util/*.c
│   │   │   └── fstab.h / fstab.c  # Stage B — /etc/fstab parse+apply, shared with --mount= CLI sugar
│   │   ├── runtime/
│   │   │   ├── runtime.h
│   │   │   ├── runtime.c          # calls each util/*.h's own native_register() once, right after wasm_runtime_full_init(); root mount established here (Stage A)
│   │   │   ├── process.h          # processes — decoupled from handles, see docs/RUNTIME.md "Processes"
│   │   │   ├── process.c          # impl: common, built entirely on runtime.h's own public API
│   │   │   ├── env.h              # export-style local/exported env split for a respawned "subshell"
│   │   │   └── env.c              # impl: common, no per-target impl
│   │   ├── app/                   # the scripted whole-process run mode, librarified out of src/linux/main.c
│   │   │   ├── app.h              # run_scripted() — applies /etc/fstab + CLI mounts (Stage B), see there for the exact split with main.c
│   │   │   └── app.c              # impl: common (port/worker.h via runtime/process.h, not raw pthread)
│   │   └── util/                  # include/…/util/*.h's *only* implementation — see "Two trees" above
│   │       ├── size.c             # impl: common + its own wasi-import NativeSymbol table/register()
│   │       ├── arena.c            # impl: common (backs memory/bytecode.c's arena too) + its own wasi-import bridge
│   │       └── log.c              # impl: common + its own wasi-import bridge
│   │
│   ├── linux/
│   │   ├── CMakeLists.txt
│   │   ├── main.c                 # thin: argv parsing + realpath only — the run mode lives in common/…/app/
│   │   └── pymergetic/metal/
│   │       ├── port/{platform,lock,worker,pipe}.c
│   │       ├── memory/{ram,kheap,bytecode}.c
│   │       ├── mount/tmpfs.c      # TMPFS fstype — impl: linux, mkdtemp() under /dev/shm + nftw() rm -rf on release
│   │       └── wasi/
│   │           ├── file.c         # Metal os_* — virtual proc + live remount; else __real_os_*
│   │           └── posix_file_real.c  # WAMR posix_file.c renamed to __real_os_*
│   │   # CMake drops stock posix_file.c from vmlib — see docs/MOUNT.md
│   │
│   ├── zephyr/
│   │   ├── CMakeLists.txt, Kconfig, prj.conf, boards/
│   │   ├── main.c                 # thin: FAT root + init — optional smoke via tests/zephyr_verify.c
│   │   └── pymergetic/metal/
│   │       ├── port/{platform,lock}.c
│   │       ├── port/worker.c                   # stub — deferred, see docs/RUNTIME.md "Bring-up plan" §5
│   │       ├── port/pipe.c                     # stub — deferred, see docs/RUNTIME.md "Bring-up plan" §5
│   │       ├── memory/{ram,kheap,bytecode}.c
│   │       ├── mount/tmpfs.c      # TMPFS fstype — impl: zephyr, stub (always fails — blocked on wasi/file.c + device.h, see docs/MOUNT.md)
│   │       └── wasi/              # private
│   │           ├── file.h
│   │           └── file.c
│   │
│   ├── nuttx/                      # NuttX app (sim first) — see src/nuttx/README.md, BASE_TOUCH.md
│   │   ├── CMakeLists.txt, Kconfig, Make.defs, Makefile, main.c
│   │   ├── configs/sim-metal.config
│   │   └── pymergetic/metal/
│   │       ├── port/{platform,lock,worker,pipe}.c
│   │       ├── memory/{ram,kheap,bytecode}.c
│   │       ├── mount/{tmpfs,device}.c   # tmpfs: mkdtemp under /tmp
│   │       └── wasi/{file,posix_file_real}.c  # linux-shaped os_* wrap for proc/live remount
│   │                                # WAMR platform/nuttx reuses posix_file + real pthread/sem
│   ├── rump/                      # [stub]
│   └── unikraft/                  # [stub]
│
│   # tests/ (process_test, thread_stress, zephyr_verify) — on archive branch only
│
├── mods/
│   ├── tests/                     # harness .wasm sources → guest /mods/tests/<name>.wasm
│   │   ├── t0_hello/main.c
│   │   ├── t_async_sleep/main.c   # EFI guest async proof (pm_metal_guest_step)
│   │   ├── t1_read/main.c
│   │   ├── t2_env/main.c
│   │   ├── t3_util_native/main.c  # util/{size,arena,log,lz4,tar}.h imports
│   │   ├── t4_getpid/ … t31_net_util/  # process/pipe/socket/tmpfs/mount/proc/net/…
│   │   └── t8_multimod_lib/ + t9_multimod_app/  # multi-module (REACTOR on t8)
│   └── apps/
│       ├── doom/                  # doomgeneric → ESP mods/apps/doom/{doom.wasm,doom1.wad}
│       └── python/                # manifest; binary from scripts/build cpython
│
├── build/                         # gitignored
│   ├── linux/runtime/
│   ├── zephyr/{native_sim,native_sim_mod,qemu_x86_64,qemu_x86_64_mod}/
│   ├── mods/tests/                # compile scratch
│   ├── guest-package/mods/{tests,apps}/  # symmetric package (all platforms)
│   ├── cpython/python.wasm
│   └── ide/
│
├── scripts/                       # setup|build|verify dispatchers
│                                  # *.d/{expect,suite,…} = agnostic; *.d/port/<plat>/ = per-host
│                                  # build.d/guest/ = wasm artifacts; setup.d/deps/; lib/
│   # patches/ — on archive branch (freestanding-efi has none)
├── docs/
├── external/                      # gitignored — plain upstream checkouts via scripts/setup
└── west-manifest/
```

---

## Header ↔ .c map

| Module | Header | `.c` (one or more) |
|--------|--------|---------------------|
| `runtime` | `src/common/…/runtime.h` | `src/common/…/runtime.c` — also owns the multi-module `module_reader`/`module_destroyer` (`PM_METAL_RUNTIME_MULTI_MODULE`-gated), see docs/RUNTIME.md "Multi-module" |
| `platform` | `src/common/…/platform.h` | `src/common/…/platform.c`? + `src/<plat>/…/platform.c` — per `impl:` tags |
| `port/lock` | `src/common/…/port/lock.h` | `src/<plat>/…/port/lock.c` — `bind`, one mutex primitive per target |
| `memory/ops` | `src/common/…/memory/ops.h` | `src/common/…/memory/ops.c` — `impl: common`, `resolve()` only, no per-target impl |
| `memory/ram` | `src/common/…/memory/ram.h` | `src/<plat>/…/memory/ram.c` — ops-struct `bind`, one getter per target |
| `memory/kheap` | `src/common/…/memory/kheap.h` | `src/<plat>/…/memory/kheap.c` — ops-struct `bind`, one getter per target |
| `memory/bytecode` | `src/common/…/memory/bytecode.h` | `src/<plat>/…/memory/bytecode.c` — ops-struct `bind`, one getter per target |
| `mount/ops` | `src/common/…/mount/ops.h` | `src/common/…/mount/ops.c` — `impl: common`, `resolve()`/`kind_by_name()` only |
| `mount/hostdir` | `src/common/…/mount/hostdir.h` | `src/common/…/mount/hostdir.c` — `impl: common` (trivial passthrough, no per-target logic needed) |
| `mount/tmpfs` | `src/common/…/mount/tmpfs.h` | `src/<plat>/…/mount/tmpfs.c` — `impl: bind`, one per target (linux: `mkdtemp()` under `/dev/shm`; zephyr: stub, blocked on `wasi/file.c`) |
| `mount/tmpfs_registry` | `src/common/…/mount/tmpfs_registry.h` | `src/common/…/mount/tmpfs_registry.c` — `impl: common`, name → host-path + refcount bookkeeping shared by every target's own `tmpfs.c` |
| `mount/populate` | `src/common/…/mount/populate.h` | `src/common/…/mount/populate.c` — `impl: common`, ustar [+ lz4] blob registry + `populate_all()` |
| `mount/table` | `src/common/…/mount/table.h` | `src/common/…/mount/table.c` — `impl: common`, the table itself; see docs/MOUNT.md |
| `mount/proc` | `src/common/…/mount/proc.h` | `src/common/…/mount/proc.c` + `proc/*` — `impl: common`, virtual proc + hooks |
| `mount/mount` | `include/…/mount/mount.h` | `src/common/…/mount/mount.c` — wasi-import bridge for privileged mount()/umount() (same shape as `util/*`) |
| `mount/fstab` | `src/common/…/mount/fstab.h` | `src/common/…/mount/fstab.c` — `impl: common`, Stage B parser/applier |
| `wasi/file` (linux) | — | `src/linux/…/wasi/file.c` + `posix_file_real.c` — Metal `os_*` (proc + live remount) |
| `wasi/file` (zephyr) | `src/zephyr/…/file.h` | `src/zephyr/…/file.c` — stub today; all `impl: zephyr` |
| `port/worker` | `src/common/…/port/worker.h` | `src/<plat>/…/port/worker.c` — `bind`, one background-thread primitive per target |
| `port/pipe` | `src/common/…/port/pipe.h` | `src/<plat>/…/port/pipe.c` — `bind`, one host pipe primitive per target |
| `runtime/process` | `src/common/…/runtime/process.h` | `src/common/…/runtime/process.c` — `impl: common`, no per-target impl; built on `runtime.h`'s own public API, no new runtime-internal locking |
| `app/app` | `src/common/…/app/app.h` | `src/common/…/app/app.c` — `impl: common`, no per-target impl; spawn()s each mod's process through `port/worker.h` (via `runtime/process.h`), not raw `pthread`/`k_thread` |
| `wasi` | `include/…/metal/wasi.h` | header-only — `PM_METAL_WASI_IMPORT()` |
| `util/size` | `include/…/size.h` | `src/common/…/util/size.c` — `impl: common` + its own wasi-import `NativeSymbol` bridge |
| `util/arena` | `include/…/arena.h` | `src/common/…/util/arena.c` — `impl: common` + its own wasi-import bridge; backs `memory/bytecode.c`'s arena |
| `util/log` | `include/…/log.h` | `src/common/…/util/log.c` — `impl: common` + its own wasi-import bridge |
| `util/lz4` | `include/…/lz4.h` | `src/common/…/util/lz4.c` — `impl: common` + its own wasi-import bridge; thin wrapper over vendored `external/lz4` (see "Vendoring") |
| `util/tar` | `include/…/tar.h` | `src/common/…/util/tar.c` — `impl: common` + its own wasi-import bridge; thin wrapper over vendored + patched `external/microtar` (see "Vendoring") — independent of `util/lz4`, a caller composes both for a compressed archive (see `mods/tests/t3_util_native`) |
| `util/crypto` | `include/…/crypto.h` | `src/common/…/util/crypto.c` — Monocypher (`external/monocypher`, `scripts/setup monocypher`) |
| `port/dns` | `src/common/…/port/dns.h` | OS DNS resolve — `impl: bind` |
| `port/udp` | `src/common/…/port/udp.h` | OS UDP open/sendto/recv/close — `impl: bind` |
| `port/tcp` | `src/common/…/port/tcp.h` | OS TCP open/bind/listen/accept/connect/send/recv — `impl: bind` |
| `port/tls` | `src/common/…/port/tls.h` | OS CA bundle path (embed PEM later) — `impl: bind` |
| `net/dns` | `include/…/net/dns.h` | portable DNS floor → `port/dns` + wasi bridge |
| `net/udp` | `include/…/net/udp.h` | portable UDP floor → `port/udp` + wasi bridge |
| `net/tcp` | `include/…/net/tcp.h` | portable TCP floor → `port/tcp` + wasi bridge |
| `net/tls` | `include/…/net/tls.h` | host TLS trust → `port/tls` (inline) |
| `net/ntp` | `include/…/net/ntp.h` | SNTP over `net/{dns,udp}` + wasi bridge |
| `net/http` | `include/…/net/http.h` | HTTP transport (curl when `PM_METAL_HAVE_CURL`) + wasi bridge; CA via `net/tls` |

---

## Common vs per-target

| Path | What |
|------|------|
| `src/common/pymergetic/metal/` | contract `.h` + any `impl: common` `.c` (incl. `util/`'s wasi-import bodies, see "Two trees") |
| `src/<plat>/pymergetic/metal/` | `impl: bind` + plat-private modules |
| `src/<plat>/main.c` | entry |

Per-function `impl:` tags in each header are authoritative — not the directory alone.

---

## Path → layer

| Path | Layer |
|------|-------|
| `include/…/` | mod contract (+ `util/` — wasi-import contract on wasm32, see "Two trees") |
| `src/common/…/port/platform.h` | port contract |
| `src/<plat>/…/port/platform.c` | port impl |
| `src/common/…/memory/*.h` | memory contracts (ops-struct `bind`) |
| `src/<plat>/…/memory/*.c` | memory impl — one ops table per module per target |
| `src/common/…/runtime/` | runtime + wamr (`runtime.h`/`.c`) + processes, decoupled from handles (`process.h`/`.c`, `impl: common`, see docs/RUNTIME.md "Processes") |
| `src/common/…/app/` | the scripted whole-process run mode (`app.h`/`.c`, `impl: common`) — every target's own `main.c` is just argv/Kconfig parsing + one call in here |
| `src/common/…/util/` | `include/…/util/*.h`'s one implementation — each `.c` also resolves its own wasm32 side, no shared bridge file |
| `src/<plat>/…/` (private) | plat-only (wasi shim, …) |
| `src/<plat>/main.c` | entry |
| `mods/` + wasi-sdk | wasm guests |

---

## Rules

| Rule | |
|------|--|
| `include/` = mod-facing only, **except** `util/` (wasi imports, one host impl; `ntp`/`http` need `PM_METAL_HAVE_NET` for real I/O) | runtime must not otherwise include from here |
| `include/…/util/*.h` | on wasm32, declares a wasi-style import (`#if defined(__wasm__)`), no local body ever exists in a mod's own `.wasm`; on the runtime's own native target, an ordinary prototype backed by `src/common/…/util/*.c` |
| Every contract function | `/* impl: common */`, `/* impl: bind */`, or `/* impl: <plat> */` |
| `.c` function order | matches `.h` declaration order |
| Skipped symbols in `.c` | `/* not impl: <tag> — <path> */` placeholder, same slot |
| `src/common/` | contract `.h` + `impl: common` `.c` |
| `src/<plat>/` | `impl: bind` + plat-private; OS `#include`s only here |
| Public symbols | `pm_metal_<module_path>_` |
| `external/` + `.tools/` | never hand-edited in place — pin + `patches/` only (below) |
| Artifacts | `build/` — gitignored |

Adapt WAMR, Zephyr, wasi-sdk, etc. from `src/` (CMake flags, shims, wrappers) first — never hand-edit a vendored tree directly (it's gitignored, so an in-place edit is invisible to git and vanishes on the next re-vendor). Patch upstream only if unavoidable (a real upstream bug with no `src/`-side workaround, e.g. a genuine data race) — see "Vendoring" below for the actual mechanism.

### Vendoring

`external/wamr` (and any future `external/<dep>`) is a plain upstream checkout pinned to one tag/commit, reproduced by `scripts/setup-<dep>.sh` (e.g. `scripts/setup wamr`) — never committed itself (gitignored), so re-running that script after `rm -rf external/<dep>` always gets back to the exact same tree. When a fix genuinely can't be done from `src/`'s side (see above), the script also applies this repo's own `patches/<dep>/NNNN-*.patch` files (in order, via `git apply`, against the pinned checkout) — those *are* tracked (plain diffs, reviewable in a normal PR), so the fix survives a fresh re-vendor without ever hand-editing the checked-out tree itself. Each patch file's own leading comment (before its `diff --git`) says which upstream bug it works around and why `src/` alone couldn't. Bump the pin + patches together, in the same change, if upstream ever fixes the same bug differently.

---

## Build outputs

| Path | Output |
|------|--------|
| `build/linux/runtime/` | `pm-linux-runtime` |
| `build/zephyr/<profile>/` | `zephyr.elf` / `zephyr.exe` |
| `build/mods/tests/` | compiled test `*.wasm` |
| `build/guest-package/` | staged `/mods/{tests,apps}/` package |
| `build/cpython/` | `python.wasm` |
| `build/ide/` | merged `compile_commands.json` |

Also gitignored: `.tools/`, `external/`, `.cache/`, `.venv/`.

---

## Builds

| Artifact | Inputs | Output |
|----------|--------|--------|
| **runtime binary** | `src/common/pymergetic/metal/` + `src/<plat>/` + WAMR + LZ4 + microtar | `build/<plat>/…` |
| **mod `.wasm`** | `mods/tests/` + wasi-sdk `wasm32-wasip1-threads` + `-I include/` | `build/mods/tests/` then packaged to `build/guest-package/mods/tests/` — guest path `/mods/tests/<name>.wasm` on every platform (`scripts/lib/guest-package.sh`; knobs `PM_METAL_GUEST_TESTS`, `PM_METAL_APP_PYTHON`). Threads/shared-memory default; `REACTOR` / `SOCKET` / `MOUNT` empty markers under `mods/tests/<name>/` as before. Apps (python) land at `/mods/apps/<name>.wasm`. |

---

## Omit (now)

```
apps/
orchestrator/ mem/ types/ …        # mod API in include/ — later
```
