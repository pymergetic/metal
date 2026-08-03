# Orchestration — three big blocks (`reg` / `py` / `wasm`)

Live Metal plan for **host** orchestration. Supersedes in-tree `mods/`
packaging, `guest/mod` FRESH/SHARED instance cages, and private µPy/wasm
heaps. Archive: [`_old/docs/MICROPYTHON.md`](../_old/docs/MICROPYTHON.md),
[`MODS.md`](MODS.md).

**Python is the OS. Metal is muscles. Wasm delivers code. `reg` is the bus.**  
**Async-first OS** — Metal runners / await / park are the default concurrency
model everywhere (py, wasm, net, ssh, http/ASGI). Sync is the exception.

---

## Locked (do not re-argue)

These are load-bearing. Agents and future-you: treat as settled.

| # | Lock |
|---|------|
| 1 | **Three blocks only:** `reg` → `py` → `wasm` (build order: reg floor, **upy first**, wasm after). |
| 2 | **Full module names** everywhere (`pymergetic.metal.fs.open`, never `fs.open`). |
| 3 | **One memory for all** — Metal TLSF / coro frames. Upy and wasm **do not** get private heaps, linear-memory cages, FRESH/SHARED instance tables, or percpu `mp_state` fuckaround. |
| 4 | **No isolation** — wasm is code delivery; py is orchestration; concurrency = Metal runners + task id. |
| 5 | **Async-first OS** — every core a runner; await = park/resume. **Py async = Metal async.** Design APIs (net, ssh, http/ASGI/microdot, fs, …) as coro/async first; do not ship a sync-primary path and bolt await on later. GC ripped out (manual/handles). |
| 6 | **`reg`:** register/export all langs; convenience by name + **ptr bind** for speed. |
| 7 | **`type=package` → `.wasm` pack** (not kernel-linked). Compiles dir + non-wasm subs; mounts `.py`. Kernel stays `type=module`. |
| 8 | **When wasm works: forge must compile Rust (and C) packs to wasm** — same `type=package` path; `impl=rs` is a first-class pack language, not host-only. |
| 9 | **Upy VM = Rust mirror** of `py/`+`extmod/`+`shared/` — see [`ORCHESTRATION_UPY_MIRROR.md`](ORCHESTRATION_UPY_MIRROR.md). No hollow stubs. |
| 10 | **Gates after big steps:** `mod check` + correctness + **bios and efi** + light perf. |

---

## Three big blocks

```text
  +------------------+
  | 1. reg/          |  cross-lang bus (full name + ptr bind)
  +--------+---------+
           ^
           | publish / lookup
  +--------+---------+     +------------------+
  | 2. py/  (upy)    |     | 3. wasm/         |
  | orchestration    |     | code delivery    |
  | Rust VM mirror   |     | load .wasm packs |
  +------------------+     +------------------+
           |                        |
           +-----------+------------+
                       v
              Metal mem / async / fs / …
              ONE allocator — all languages
```

| Block | Dir | Criteria |
|-------|-----|----------|
| **1. `reg`** | `src/.../reg/` | Full module name + func (+ later class/vars). Register from any lang; export to any lang. Name call + ptr bind. One table for kernel and loaded code. |
| **2. `py`** | `src/.../py/` | Upy-first. Rust-rebuild VM/builtins ([inventory](ORCHESTRATION_UPY_MIRROR.md)). Py await = Metal async. GC out. **Same Metal alloc.** |
| **3. `wasm`** | `src/.../wasm/` | Load packs → register into `reg`. **Same Metal alloc — one memory, no wasm isolation.** Packs from C **and Rust** (`impl=rs` → `.wasm`). Subs + mounted `.py` in the pack. Importlib resolve later. |

Engines under `external/` are reference/link inputs (`micropython`, `wamr`). No in-package `mods/` orchestration.

---

## Naming (locked)

```text
pymergetic.metal.fs.open     # yes
fs.open                      # no
```

---

## `reg` contract

```text
register(full_module, func, face)   # host link-time or wasm load
bind(full_module, func) -> ptr      # hot path
lookup / call by name               # convenience
```

---

## One memory / async-first (all blocks)

- **Async-first OS:** primary APIs park on Metal await; every core a runner.
  Sync wrappers only when forced (bring-up, tiny helpers) — never the design.
- **One heap:** `pm_metal_mem_*` / coro frames for kernel, upy, and wasm.
- **No** wasm linear-memory-as-private-heap, no upy blob heap, no instance cages.
- Stackless across `await`; N equal runners; process ≈ task id.
- Shared state across await/cores: real sync (mutex/spin / Meyers) — project-wide rule.
- Ownership/handles — not GC finalizers.

---

## Full host tree — upy rebuilt in Rust

**Rule:** `external/micropython/{py,extmod,shared}` is **reference only**.
The running VM is Rust under `src/pymergetic/metal/py/upy/`.

| Rule | Detail |
|------|--------|
| Header mirror | Every upstream `.h` → `upy/**/foo.rs` (types/API) |
| Body rewrite | Every needed `.c` → Rust (not eternal C wrappers) |
| Builtins | All `mod*` / `builtin*` / `obj*` **and** pure-Python core (asyncio) → Rust. No VM-core `.py` builtins |
| GC / threads / upy scheduler | `DEAD` — Metal alloc + Metal runners |
| Arch | x86_64 only (`asmx64`, `nlrx64`, `emitnx64`); other asm/nlr/emit = `OMIT_ARCH` |
| HW / upy-net / upy-fat-lfs | `OMIT_HW` — Metal owns devices/FS/net |
| No hollow files | Do **not** land empty stub `.rs`. Finish a row or leave it undone in the inventory |
| No cages | No `py_ctx` isolation / private heaps |

**Complete per-file inventory (356 rows):**  
[`ORCHESTRATION_UPY_MIRROR.md`](ORCHESTRATION_UPY_MIRROR.md) — every upstream
file tagged `MIRROR` / `REWRITE` / `DEAD` / `OMIT_*` / `TOOL` / `SHARED_OPT`.
**197** rows are `MIRROR`+`REWRITE` work. That file is the checklist; this
section is the layout.

```text
external/
  micropython/                      # REFERENCE submodule (py/ extmod/ shared/)
  wamr/                             # phase C

src/pymergetic/metal/
  reg/
    .pm/{module,Cargo.toml,smoke.rs}
    __init__.rs                     # pm_metal_reg_*
    table.rs
    bind.rs
    publish.rs

  wasm/                             # phase C
    .pm/{module,Cargo.toml,smoke.rs}
    __init__.rs                     # pm_metal_wasm_*
    step.rs
    load.rs
    port/{runtime.c,runtime.h}

  py/                               # phase B — impl=rs
    .pm/{module,Cargo.toml,build.rs,smoke.rs}
    __init__.rs                     # pm_metal_py_* border
    loop.rs                         # rebuilt main loop (Metal runners)
    async_bridge.rs
    step.rs
    bind.rs                         # -> reg (full module names)
    handle.rs
    alloc.rs                        # -> pm_metal_mem_*
    gc_off.rs
    libc_policy.rs
    shell.rs
    port/                           # thin C edge only
      mpconfigport.h
      mphalport.h
      mphalport.c
      stubs.c
    upy/
      py/                           # ALL py/*.h + non-obj/mod .c  (see inventory)
        builtin/                    # ALL py/mod*.c + builtin*.c
        objects/                    # ALL py/obj*.c
      extmod/                       # MIRROR subset + asyncio/ REWRITE
      shared/                       # SHARED_OPT when needed (libc/runtime/…)

tests/
  wasm_hello/                       # phase C′ type=package
    .pm/module
    __init__.rs
  py_smoke/                         # bios+efi proofs (app .py OK here)
    hello.py

build/
  py/                               # qstr / module-defs (TOOL replacements)
  tests/                            # packed .wasm
```

**Layout map**

| Upstream | Live path | Notes |
|----------|-----------|-------|
| `py/*.h` | `upy/py/*.rs` | 66 headers; see inventory tags |
| `py/*.c` (vm/runtime/…) | `upy/py/*.rs` | |
| `py/mod*.c`, `builtin*.c` | `upy/py/builtin/*.rs` | |
| `py/obj*.c` | `upy/py/objects/*.rs` | |
| `py/make*.py` | forge/`build/py/` | `TOOL` — not runtime |
| `extmod/*` keep-list | `upy/extmod/*.rs` | json/os/time/re/… + thin vfs |
| `extmod/asyncio/*.py` | `upy/extmod/asyncio/*.rs` | `REWRITE` → Metal async |
| `extmod` HW/lwip/fat/lfs/… | — | `OMIT_HW` |
| `shared/libc|runtime|…` | `upy/shared/…` | `SHARED_OPT` / `DEAD` gchelper |

**Shim map (Metal edge)**

| Concern | Lives in | Talks to |
|---------|----------|----------|
| Public C ABI | `py/__init__.rs` | boot / shell / tests |
| Main loop | `py/loop.rs` | `async/` runners |
| VM step | `upy/py/vm.rs` + `py/step.rs` | runners |
| Await | `py/async_bridge.rs` | `pm_metal_async_*` |
| Alloc | `py/alloc.rs` + `upy/py/malloc.rs` | `pm_metal_mem_*` |
| No GC | `py/gc_off.rs` | stock GC `DEAD` |
| Binds | `py/bind.rs` | `reg/` |
| Builtins | `upy/py/builtin/*` | `reg` / Metal faces |
| Objects | `upy/py/objects/*` | VM |
| Console | `port/mphalport.c` | `console` / `serial` |
| Libc | `libc_policy.rs` | Metal and/or `upy/shared/libc` |

---

## Gates — correctness, performance, BIOS + UEFI

After every **big step** (reg smoke, upy loop/alloc/GC-off, await bridge,
wasm load, package pack):

1. **Correctness**
   - `./forge-cli mod check` (face symmetry c/rs/py)
   - Host smoke for the tier touched (`.pm/smoke.rs` / `forge mod test` when wired)
   - Boot proof script or serial expect where the feature is visible
2. **Both firmware paths**
   - `./forge-cli build bios && ./forge-cli run bios` (or stress subset)
   - `./forge-cli build efi && ./forge-cli run efi`
   - Same gate on both — do not ship “EFI-only” or “BIOS-only” for orchestration work
3. **Performance (light, honest)**
   - After loop + alloc + await bridge: measure a tight Python step / await
     round-trip vs a native Metal coro (serial or host smoke counters)
   - After wasm load: load + `reg.bind` + call latency vs native ptr bind
   - No fake benches; one real number, regress if it blows a recorded budget
4. **Alloc discipline**
   - Grep / smoke: upy and wasm paths must not grow a private heap or
     re-enable stock GC “just for bring-up”
5. **Upy mirror discipline** (phase B+)
   - Track [`ORCHESTRATION_UPY_MIRROR.md`](ORCHESTRATION_UPY_MIRROR.md):
     every `MIRROR`/`REWRITE` claimed done has finished `.rs` (no hollow stubs)
   - Diff inventory vs `external/micropython/{py,extmod,shared}` after vendor
   - No core builtin left only as `.py` or stock `mod*.c`

---

## `.pm/module` `type`: kernel vs wasm package

Documented in [`definitions/module.md`](definitions/module.md)
(Markers). Live meaning under this plan:

| `type` | Role |
|--------|------|
| `module` | **Kernel / firmware-resident** — linked into the image; faces sync; registers into `reg` at bring-up. This is everything under `src/pymergetic/metal/**` today. |
| `package` | **Wasm pack** — forge compiles the dir to `.wasm` (**C and Rust** `impl`); **not** kernel-linked. Load via `wasm/`; exports → `reg` (full names). **Same Metal memory** as everything else. |
| `hidden` | Port / namespace shell — no codegen. |

**Pack contents:** directory → image: native/rs/c **and nested subs** (unless
sub is its own `type=package`). Loose `.py` **mounted**.  
**Rust → wasm:** once the wasm host works, `impl=rs` packages must pack
through the same forge path as C (no “Rust host-only” ghetto).  
Importlib resolve = after wasm works.

```text
tests/wasm_hello/            # type=package, impl=rs  →  .wasm via forge
tests/wasm_hello_c/          # type=package, impl=c   →  same pipeline
```

Kernel stays `type=module`. First packs under `tests/`, not firmware.

---

## Phased delivery (**upy first**)

| Phase | Deliver |
|-------|---------|
| **A. `reg`** | Register + lookup + ptr bind (needed so py/wasm have one bus) |
| **B. `py` / upy** | Vendor µPy as **reference**; execute [`ORCHESTRATION_UPY_MIRROR.md`](ORCHESTRATION_UPY_MIRROR.md) (`MIRROR`+`REWRITE` ≈197 rows — finished Rust only, no hollow stubs); Metal loop/alloc; **rip GC**; thin C port; libc policy. **Gate:** inventory + mod check + bios + efi + light perf. |
| **C. `wasm`** | Host load → `reg`; **one Metal memory, no isolation**. **Gate:** bios + efi. |
| **C′. packs** | `tests/` `type=package`: forge pack **C and Rust** → `.wasm` (+ subs / `.py` mount); load/call. **Gate:** bios + efi. |
| **D. Wire** | Kernel faces auto-register; importlib resolve; class/static bars in `reg` |
| **E. Later** | Custom mem tracking for all languages; REPL as shell |
| **F. Dropbear** | After orchestration waves: finish `net/ssh/` (dirs exist); AUTORUN W8 |
| **G. `net/http` complete** | After Dropbear: finish ASGI in `http/server` + Microdot runner under `http/microdot/`; AUTORUN W9 |
| **H. Registry unify + reconnect** | Always-proxy cached-slot faces for floor modules; reconnect `py`/`wasm`/`net`/`fs` to boot; AUTORUN W10 |
| **I. REPL as shell + upy finish** | Finish upy VM (lexer/compile/emit/repl); REPL replaces shell; async concurrency metrics + cross-lang/wasm stress; AUTORUN W11 |
| **J. gfx + drivers** | Rust `dev/gfx` dispatch + QEMU backends + HW ports; minimal UI consumer; AUTORUN W12 |
| **K. Doc-browser** | VFS source browse + forge in-kernel render + Rust Microdot rewrite + self-serve binary download; AUTORUN W13 |
| **L. Signed pkg fetch** | `trust/` verify + HTTP fetch-on-miss for wasm/AOT; AUTORUN W14 |
| **M. metal-doom validation** | Separate repo `packages/metal-doom` on a **new branch** (never its `main`); AUTORUN W15 |
| **N. Guest dual-ABI** | `guest_surface` faces + host WAMR natives for kernel modules doom needs; AUTORUN W16 (unblocks W15.2) |

Do **not** rebuild `_old` FRESH/SHARED instance machinery or a linux twin as the primary path.
**Shape rule for H–N:** current module/call paradigm (`.pm/module`, generated faces, always-proxy cached-slot, quiesce, `guest_surface`/`PM_METAL_PKG_IMPORT`) outranks every `_old`/`main` reference — those are behavior only.

---

## Face symmetry check (all languages)

Public border functions on each human stem must be available on **every**
lang-pool face (`c` / `rs` / `py`). No accidental hide behind one language.

```text
./forge-cli mod check   # exit 1 on skew / missing face
```

For each module stem with a non-empty fn border:

1. Parse the human impl (`.rs` / `.h` / …) → expected fn name set  
2. For each emitted face (not the impl’s own slot): require the face file  
3. Parse the face → fn names must match exactly (missing / extra = fail)

Human Rust borders are `#[no_mangle] extern "C" fn` only (foreign
`extern "C" { }` import blocks are ignored). Generated faces also scrape
`extern "C" { pub fn }` and `#[inline] pub [unsafe] fn` twins. `.pyi`
stubs are `def name(...): ...` lines.

This is the gate before wiring `reg` auto-publish and Python import. Run
after `forge mod sync` when faces change.

---

## Explicitly rejected (live)

- Wasm / upy **isolation**, private heaps, linear-memory cages, FRESH/SHARED  
- “Wasm can’t do Rust packs” / Rust host-only while C packs exist  
- Short registry names; per-language bind tables  
- In-package `mods/` orchestration; core builtins left as `.py` / stock `mod*.c`  
- Hollow stub `.rs`; orphan headers with no inventory row  
- Re-litigating the **Locked** table above without an explicit plan change  

---

## Doc map

| Doc | Role |
|-----|------|
| **This file** | Live plan — **Locked** table is the short form |
| [`ORCHESTRATION_AUTORUN.md`](ORCHESTRATION_AUTORUN.md) | Unattended continue runbook (no user prompts) |
| [`ORCHESTRATION_PROGRESS.md`](ORCHESTRATION_PROGRESS.md) | Chunk checklist / handoff between continues |
| [`ORCHESTRATION_UPY_MIRROR.md`](ORCHESTRATION_UPY_MIRROR.md) | Full upy→Rust file inventory |
| [`PLATFORM.md`](PLATFORM.md) | Firmware ladder |
| [`MODS.md`](MODS.md) | Archive mod/process model |
| [`MEMORY.md`](MEMORY.md) / [`COOP_MEMORY.md`](COOP_MEMORY.md) | Alloc / stackless |
