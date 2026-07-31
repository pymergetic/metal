# Orchestration — three host tiers (`reg` / `wasm` / `py`)

Live Metal plan for **host** orchestration. Supersedes in-tree `mods/`
packaging, `guest/mod` FRESH/SHARED instance cages, and private µPy/wasm
heaps for the direction below. Historical product detail remains in
[`_old/docs/MICROPYTHON.md`](../_old/docs/MICROPYTHON.md) and
[`MODS.md`](MODS.md) (archive-shaped; do not treat FRESH cages as the
live target).

**Python is the OS orchestration layer. Metal is muscles.** Wasm delivers
code. **`reg`** is the cross-language call bus.

---

## Three tiers

```text
                    +------------------+
                    |  py/  (upy)      |  orchestration; py async == Metal async
                    +--------+---------+
                             | lookup / bind on import
                    +--------v---------+
                    |  reg/            |  full module name + func (+ later class/vars)
                    |  name + ptr bind |  register/export all languages
                    +--------+---------+
                             ^
              register       |       register
           (kernel/host)     |    (wasm load)
                    +--------v---------+
                    |  wasm/           |  load code only; Metal alloc; no cages
                    +------------------+

Metal mem / async / fs / …   <--- all durable alloc and park/resume
```

| Tier | Module | Job |
|------|--------|-----|
| **1. `reg`** | `src/pymergetic/metal/reg/` | Flat registry: **full** module name + func (later class / statics). Register from **any** language; export to **any** language. Convenience (by name) + **ptr bind** (resolve once, hot call). |
| **2. `wasm`** | `src/pymergetic/metal/wasm/` | Host engine border. Loads images that **deliver code** (C/rs/py payloads; optional in-image sources like the old kernel pack — when finished). **No** instance isolation / private linear heaps / FRESH cages. Durable memory = `pm_metal_mem_*` / coro frames. Async rules = Metal rules (no naked statics across await; shared state needs real sync). |
| **3. `py`** | `src/pymergetic/metal/py/` | µPy attach as main internal orchestration. Every core is a runner; **Python `await` = Metal park/resume**. Stock GC **ripped out** — manual / scope / handles (C-ish Python). Normal Metal alloc only — no upy blob heap / percpu `mp_state` fuckaround. On import: query `reg`, expose into Python. |

Engines stay **vanilla** under `external/` (`micropython`, `wamr`, …). Ports are thin. Package-root **`mods/`** is **not** a live Metal concept (external repos / staging elsewhere).

---

## Naming (locked)

Registry keys always use the **full module path** (same discipline as
`pm_metal_<module>_…` ABI prefixes):

```text
pymergetic.metal.fs.open     # yes
fs.open                      # no
```

---

## `reg` contract

```text
register(full_module, func, face)   # host link-time or wasm load
bind(full_module, func) -> ptr      # hot path; no string walk later
lookup / call by name               # convenience (import, REPL, glue)
```

- One table for kernel and loaded code — no shadow “py binds” vs “wasm natives”.
- Forge may later auto-emit register rows from module exports (C/rs/py/wasm symmetry).
- Python import = `reg` lookup (flat map is fine until measured otherwise) then bind into the Python namespace.

---

## Async / memory (all three)

- Stackless: nothing durable on C/wasm/upy call stack across `await`.
- Alloc: Metal TLSF / coro frames only.
- Runners: N equal cores; process ≈ task id; no GIL story, no upy-private parallel heaps.
- Wasm/Python coding style: ownership and handles; no relying on GC finalizers.

---

## Proposed tree (host only)

```text
external/
  micropython/                 # vanilla submodule
  wamr/                        # vanilla submodule

src/pymergetic/metal/
  reg/
    .pm/module
    __init__.rs                # pm_metal_reg_*  register / lookup / bind
  wasm/
    .pm/module
    __init__.rs                # pm_metal_wasm_* load / find export / call
    step.rs
    port/                      # runtime glue; Metal alloc
  py/
    .pm/module
    __init__.rs                # pm_metal_py_*  init / step / call
    bind.rs                    # import path -> reg
    step.rs                    # bytecode slice + Metal await
    port/                      # mphal, mpconfig -> pm_metal_*
    embed/                     # build-owned qstr/frozen glue

build/
  micropython_embed/           # generated link inputs
```

---

## `.pm/module` `type`: kernel vs wasm package

Documented in [`definitions/module.md`](definitions/module.md)
(Markers). Live meaning under this plan:

| `type` | Role |
|--------|------|
| `module` | **Kernel / firmware-resident** — linked into the image; faces sync; registers into `reg` at bring-up. This is everything under `src/pymergetic/metal/**` today. |
| `package` | **Wasm delivery unit** — build to a `.wasm` (pack), **not** in the kernel link. Load via `wasm/`, exports land in `reg` under the **full** module name. Same lang-pool faces (`c`/`rs`/`py`); payload is code delivery, Metal alloc. |
| `hidden` | Port / namespace shell — no codegen. |

Kernel stays `type: module` on purpose: residents are muscles, not
loadable packs. First real `type: package` belongs under package-root
`tests/` (e.g. `tests/wasm_hello/`) once phase **B** can load an image —
not inside the firmware tree.

```text
tests/wasm_hello/
  .pm/module                 # type=package, name=tests.wasm_hello, impl=rs|c
  __init__.rs                # tiny border -> reg on load
  # forge pack -> build/tests/wasm_hello.wasm
  # boot/smoke: wasm load -> reg.bind("tests.wasm_hello", ...) -> call
```

---

## Phased delivery

| Phase | Deliver |
|-------|---------|
| **A. `reg`** | Register + lookup + ptr bind; host modules can publish rows; smoke from C/rs |
| **B. `wasm`** | Load image, register exports into `reg`, call via ptr bind; Metal alloc; no cages |
| **B′. package test** | First `type=package` under `tests/` packed to `.wasm` and loaded (not kernel-linked) |
| **C. `py`** | Link vanilla upy; step + serial out; import uses `reg`; GC off / manual style |
| **D. Wire** | Kernel faces auto-register; py async proofs; optional in-wasm sources (old “nice” pack) |
| **E. Later** | Custom mem GC/tracking for all languages; class/static bars in `reg`; REPL as shell |

Do **not** rebuild `_old` FRESH/SHARED instance machinery or a linux twin as the primary path.

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

- In-package `mods/` orchestration framework  
- Wasm/upy private heaps or percpu interpreter ctx tables  
- Short registry names (`fs` instead of `pymergetic.metal.fs`)  
- Separate bind tables per language  
- Claiming isolation/parallel µPy heaps without a real Metal-wide GC design  

---

## Doc map

| Doc | Role after this plan |
|-----|----------------------|
| **This file** | Live host orchestration plan |
| [`PLATFORM.md`](PLATFORM.md) | Firmware ladder; points here for py/wasm/reg |
| [`MODS.md`](MODS.md) | Historical mod/process/instance model — reference only for live host |
| [`_old/docs/MICROPYTHON.md`](../_old/docs/MICROPYTHON.md) | Archive spike detail |
| [`MEMORY.md`](MEMORY.md) / [`COOP_MEMORY.md`](COOP_MEMORY.md) | Alloc / stackless rules (still load-bearing) |
