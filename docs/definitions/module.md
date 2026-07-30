# Definitions — polyglot module

How a Metal **module** is laid out so any of C / C++ / Rust / Python can
be the implementation language, while every language consumes one
unified public surface. Experimentation lives under `exp2/`; product
`src/` / `include/` migrate toward this over time.

**Tooling / CLI workflow:** [`docs/TOOLING.md`](../TOOLING.md) (`metal`
under `tools/metal/`).

---

## Goals

- **One module folder** under `src/…` — no parallel handwritten `include/`
  twin for new modules (`include/` collapses into `src`; `-I src`).
- **Any language may implement**; any language may consume.
- **Lang pool** — one impl in; emit the other pool faces (see below).
  `cpp` shares the `c` slot; `toml` is output-only and not default.
- **Runtime border is C** — generated `__init__.h` (or sibling-stem
  faces) is what languages link when the `c` slot is emitted; no
  Py↔Rust shortcuts.
- **Borders stay small:** hidden libs (e.g. TLSF) are never part of the
  public catalog.
- **Codegen must not overwrite human code** — gated by the in-file
  ownership banner (`DO NOT HAND-EDIT THIS FILE.`).

---

## Layout

```text
<path/to/module>/                 # e.g. src/pymergetic/metal/dt/
  __init__.rs                     # HUMAN — package entry when impl=rs
  …                               # other HUMAN sibling stems (same lang)
  __init__.h                      # GENERATED — C pool face (when impl slot != c)
  __init__.pyi                    # GENERATED — py pool face (when impl != py)
  .pm/
    module                        # HUMAN — JSON metadata (required)
    Cargo.toml                    # HUMAN — Rust crate (when impl=rs)
    build.rs / smoke.*            # optional schema
  nested/                         # optional nested Metal module
    .pm/module
    __init__.rs
  platform/                       # example: nested C module (boot ops)
    .pm/module                    # impl=c
    *.h                           # HUMAN C stems
    bios/ efi/                    # hidden ports (.pm/module type=hidden)
```

| Path | Who writes it |
|------|----------------|
| `.pm/module` | human (JSON) |
| `.pm/Cargo.toml`, `.pm/build.*`, `.pm/smoke.*` | human |
| `__init__.{impl_ext}` and other impl sources | human |
| `type=hidden` trees | human — never codegen |
| pool faces (`*.h` / `*.rs` / `*.pyi` / optional `*.toml`) | **codegen** (banner-owned) |

Human and generated files share the module root (except schema under
`.pm/`). Ownership is the in-file banner, not a separate `_impl/` tree.

### Banner write gate (hard)

Before writing output `REL` (path relative to the module root):

1. Module has `.pm/module` with `type` `module` or `package`.
2. `REL` is not under `.pm/` and not under any `type=hidden` subtree.
3. If `mod/REL` **exists** and does **not** contain
   `DO NOT HAND-EDIT THIS FILE.` → **refuse** (human-owned).
4. Otherwise write `REL` (content must include that banner line).

**Entry symmetry:** package entry is usually `__init__` + `impl` extension
(for `impl=c`, prefer `__init__.h` then `__init__.c`). Sibling-only modules
are allowed when every public stem is a sibling (e.g. `boot/platform/` with
`uart.h`, `mem_map.h`, … and no umbrella `__init__.h`). Generated faces
are the **other** pool extensions of each stem. Sync never emits the impl’s
own slot (no second `uart.h` when `impl=c`).

### Module-local `.gitignore` (generated outputs)

`metal mod sync` also updates a **dir-local** `.gitignore` in the module
root. It rewrites a managed block so clones ignore projections without a
repo-wide allowlist:

```gitignore
# BEGIN metal-generated
.target/
target/
__init__.h
__init__.pyi
__init__.toml
# END metal-generated
```

`__init__.toml` appears in the ignore block when emitted (`--emit toml`).
`metal mod clean` removes it with every other banner-owned face.

The `.gitignore` file itself is committed. Do not hand-edit the managed
block; do not commit generated faces. Human sources (`__init__.{ext}`,
`.pm/module`, …) stay tracked.

Port contracts for multi-target firmware belong in a nested C module
(e.g. `boot/platform/*.h`) with `bios/` / `efi/` as **hidden** children —
not a `common/` parking lot beside a Rust `impl`.

---

## Lang pool (fixed codegen scheme)

**Rule:** there is a small **lang pool**. Sync takes one impl language as
input and emits the **other** pool faces. Conversion always goes through
an in-memory catalog for that run; faces are never written by skipping
the catalog object.

```text
  .module impl = rs | c | cpp | py     (exactly one; cpp → pool slot c)

              EXPORT  (source → in-memory catalog)
                         │
                         ▼
              EMIT each other pool slot
                         │
         ┌───────┬───────┼───────┐
         ▼       ▼       ▼       ▼
        c       rs      py     toml*
      *.h    *.rs    *.pyi  *.toml

  * toml is output-only (never an impl) and NOT emitted by default
    (opt in: metal mod sync --emit toml)

  Never: any face → human {base}.{impl_ext}
```

| Pool slot | Default emit | Can be impl? | Face |
|-----------|--------------|--------------|------|
| `c` | yes | yes (`impl=c` or `impl=cpp`) | `{base}.h` |
| `rs` | yes | yes | `{base}.rs` |
| `py` | yes | yes | `{base}.pyi` |
| `toml` | **no** | **no** | `{base}.toml` |

Own slot is skipped: `impl=rs` → emit `c`+`py`; `impl=c`/`cpp` → emit
`rs`+`py`; etc. Stale marker-owned faces not in this run’s emit set are
pruned (e.g. leftover `{base}.toml` after a default sync).

### Border vs interior

When the `c` slot is emitted, that `{base}.h` is the **runtime border**:
`extern "C"`, `pm_metal_…`. C++ consumers just `#include` it — no separate
C++ wrapper face. Interior of the module may use Rust `api::`, C++
templates, etc.

### Consume foreign modules (no ABI twins)

Call another module through **its** lang-pool face for your language.
Do not redeclare its structs / `extern "C"` / private twin headers.

| Provider `impl` | C consumer | Rust consumer |
|-----------------|------------|---------------|
| `rs` / `py` | generated `{base}.h` | generated `{base}.rs` |
| `c` / `cpp` | human `{base}.h` | generated `{base}.rs` |

Example: `boot` (Rust) uses `#[path = "platform/uart.rs"]` (generated
from human `uart.h`). BIOS C includes human `platform/*.h` and
generated `mem/__init__.h` / `dt/__init__.h`. Safe wrappers may wrap the
face; they must not copy it.

### Practical tool map (incremental)

Pipeline stays **Python + almost no libs** (upy / in-kernel later). No
cbindgen, libclang, PyPI parsers in `metal_cli/mod/**`.

| Export (→ catalog) | Mechanism |
|--------------------|-----------|
| `impl = rs` | Python extract over `{base}.rs` (`rust_export.py`) |
| `impl = c` / `cpp` | later — scan prototypes / `extern "C"` |
| `impl = py` | later — export declared surface (+ C trampolines on emit) |

| Emit (catalog →) | When |
|------------------|------|
| `{base}.h` | pool slot `c` and impl slot ≠ `c` |
| `{base}.rs` | pool slot `rs` and impl ≠ `rs` |
| `{base}.pyi` | pool slot `py` and impl ≠ `py` |
| `{base}.toml` | only with `--emit toml` |
| Python runtime bind | later — register/attach C→Python when `impl != py` |

**Dep budget:** `re`, basic containers, tiny hand parsers.

---

## Markers — `.pm/module` + `type`

One JSON file per directory: `.pm/module`. The former root files
(`.module` / `.package` / `.nomodule`) are obsolete.

| `type` | Role |
|---------------|------|
| `module` | **Kernel module** — sync/codegen; firmware link when applicable. |
| `package` | **Package** — WASI / `metal pack` candidate; not in the kernel link by default. |
| `hidden` | **Forbid** — plain port or namespace shell; tools refuse codegen. |

```json
{
  "type": "module",
  "name": "pymergetic.metal.dt",
  "impl": "rs",
  "version": "0.1.0"
}
```

`hidden` may be only `{ "type": "hidden" }`. Rust crates also keep
`Cargo.toml` (and optional `build` / `smoke`) under `.pm/`.

Public pool faces are emitted from the in-memory catalog — never
hand-written without the ownership banner. `{base}.toml` is optional
(`--emit toml`), not required for sync.

**Tool checks before writing into `DIR`:**

1. `DIR/.pm/module` exists with `type` `module` or `package`.
2. Output path passes the banner write gate above (never under `.pm/` or
   a `hidden` subtree).
3. Prefer known pool face basenames (`{base}.h`, `{base}.rs`,
   `{base}.pyi`, optional `{base}.toml`).

---

## One implementation language

Hard rule: a module’s impl sources use **exactly one** language, matching
`impl` in `.module`.

- No mixing `.c` and `.rs` as the module body.
- Switching language = replace impl sources and update `.module`.
- Private headers for a C/C++ impl stay beside the sources (or under a
  private name) — never projected as public catalog symbols.

Vendored or internal libraries used by the impl (TLSF, etc.) are
**manual imports / link-only**. They are not re-exported in the public
catalog.

---

## Human original `__init__.{ext}`

Package entry is **always** `__init__.{impl_ext}` — same name in every
language. No per-module stem alias (`mem.rs` / `dt.rs` are wrong).

| `impl` | Package entry |
|--------|---------------|
| `rs`   | `__init__.rs` |
| `c`    | `__init__.c` |
| `cpp`  | `__init__.cpp` |
| `py`   | `__init__.py` |

Generated faces = other extensions of that stem (`__init__.h`,
`__init__.pyi`, …). Sync must not overwrite without the ownership banner.

**Directory = Metal module** (`.module`). Nested packages are nested
dirs with their own `.module` + `__init__.{ext}` (e.g.
`mem/tlsf/__init__.rs`).

**Sibling sources** in the same dir are extra stems sync may codegen
when they carry a pool border. Prefer nested dirs with their own
`.module` + `__init__.{ext}` for real subpackages (`mem/arena/`,
`mem/tlsf/`). Vendored C stays external (FFI/shim).

**Private `_*.{ext}` stems:** faceless helpers wired only via `#[path]` /
same-TU include. They must not appear as sync stems and must not get
pool faces. Name them with a leading underscore (`_line.rs`,
`_registry.rs`, `_stack.c`).

**No umbrella re-exports:** a package-marker `__init__` (empty catalog,
sync `§ - ~`) must not re-export sibling C borders as its own ABI.
External consumers always hit the concrete stem (`print`, `spin`,
`endian`). Rust `pub use` inside a crate for internal wiring is fine.

**Header-only `static inline`:** a real public border. Catalog those
prototypes/definitions (`inline=true`); emit a ported Rust face
(`#[inline] pub fn`, not `extern "C"` — cannot link to another TU’s
`static inline`) and a normal `.pyi`. Sync row is still `§ * * -`.
C human source may stay header-only (no `.c` body).

**Empty catalogs:** a stem with no exported fns does **not** get `.h` /
consumer `.rs` faces. Exception: package entry still gets `__init__.pyi`
(even if empty) as a typing/package marker when the py slot is emitted.
Never auto-generate `__init__.py` — that is only human source when
`impl=py`. Sibling stems without a border stay faceless.

**Non-goal (for now):** automatic cross-lang include→import graph.
Prefer explicit stem imports + `_`-private helpers over a project-local
resolver that duplicates Cargo/link deps.

### Forge (codegen engine module)

[`exp2/src/pymergetic/metal/forge/`](../../exp2/src/pymergetic/metal/forge/)
is a normal Metal module (`impl=rs`, lang-pool faces on `__init__`) that
*implements* module sync/check. Per pool language: `_import_{c,rs,py,toml}`
(source → catalog) and `_export_{c,rs,py,toml}` (catalog → face); stubs
allowed until wired. `_gitignore` owns the managed ignore block.
`_port/{solo,metal}` implement **`ForgeStore`** / **`ForgeSession`**.
Ops return **`ForgePoll`** (`Ready` / `Pending`); solo is always-Ready.

| Face | Port | Hosting |
|------|------|---------|
| Outside | `_port/solo` shim only | App [`tools/forge-cli/`](../../tools/forge-cli/) (launcher [`tools/forge`](../../tools/forge)) |
| Inside (later) | `_port/metal` | Metal command → `fn_process` + console attach |

Prebuild faces with solo (or today’s Python `metal mod sync`); later the
binary can keep human stems only and emit other pool faces on demand.
Forge is **not** linked into EFI by default. Python `tools/metal` remains
authoritative until forge is confirmed.

### Fixed v2 schema files (private)

All under `.pm/` (never codegen targets):

| File | Role |
|------|------|
| `.pm/module` | JSON metadata (`type`, `name`, `impl`, …) |
| `.pm/Cargo.toml` | Rust crate (`[lib] path = "../__init__.rs"`) |
| `.pm/build.{ext}` | Native/tooling build hook |
| `.pm/smoke.{ext}` | Host smoketest — `metal mod test` |

Primary smoke usually matches `.pm/module` `impl`. Extra `.pm/smoke.c` /
`.pm/smoke.py` (etc.) may sit beside an `impl=rs` module to prove the C/Py
ABI against the host library; `metal mod test` runs every `.pm/smoke.*` present.

| Lang | Meaning of `__init__.{ext}` |
|------|-----------------------------|
| Rust   | Crate / lib root (`.pm/Cargo.toml` `path = "../__init__.rs"`) |
| C/C++  | Header-first API (`.h`) and/or primary `.c`/`.cpp` TU |
| Python | Package init |

### No random work on module load

Hard line for Metal modules (runtime, not the filename):

- **Ready `…_init` (catalog)** = explicit runtime bring-up (boot or
  caller). Never implicit “static constructors that steal the show.”
- **`__init__.{ext}`** = source entry only. Safe: child modules, type
  tables, cheap attach hooks.
- **Forbidden as module contract:** heap/network/device bring-up, threads,
  or other side effects just because something was imported or linked.

### Python consumer packaging (special, thin)

When `impl != py`, sync emits `{base}.pyi` (same stem — stubs for the
import face, not a second package init). Register/attach binds C hub
symbols into the Python module — no per-symbol hand registration. Real
bring-up APIs (`…_init`) stay explicit calls.

### n-to-m language bindings later

Always via catalog → C border. Cross-language init never means “import A
secretly inits B’s heap.”

---

## Public surface (ready symbols)

Only **ready** symbols appear on the C hub and thus on consumer faces:

- Stable Metal API (`pm_metal_mem_alloc`, …).
- Registration / named catalog for wasm and Python attach.
- **Not** TLSF entry points, bitmaps, private helpers.

### Unified consumer view

Same module path, language = suffix — all meaning “bind the hub `.h`”:

```text
…/mem/__init__.rs       # HUMAN when impl=rs
…/mem/__init__.h        # GENERATED C hub
…/mem/.pm/module        # JSON metadata
…/mem/.pm/Cargo.toml    # Rust crate
…/mem/.pm/build.rs      # schema: cargo/native build hook
…/mem/.pm/smoke.rs      # schema: host smoke (metal mod test)
…/mem/arena/__init__.rs # nested module
…/mem/tlsf/__init__.rs  # nested module
```

Same `__init__` stem throughout. When `impl=rs`, there is no generated
`__init__.rs`.

---

## Build / consume sketch

```text
.module + __init__.{ext}
        │
        ├─ metal mod sync
        │     1) EXPORT  impl → __init__.h  (+ C glue if needed)
        │     2) IMPORT  __init__.h → missing faces (.rs / .pyi / …)
        │        (+ ownership banner, module .gitignore)
        │
        └─ compile human sources (+ any generated C glue) → link
```

Guest / mod builds: `-I src` (or the package root that contains these
module paths). No separate handwritten `include/pymergetic/…` for modules
that have adopted this layout.

---

## North star — one import system (metal-py + wasm)

The long-term overlap of the four languages is not “make C feel like
Python syntax.” It is: **everything that enters the runtime is a
module in one tree**, and **metal-py’s import machinery** (closely tied
to wasm load) is how that tree is walked.

```text
import foo.bar
        │
        ▼
 metal-py import / module loader
        │
        ├─ resolve foo.bar as a package in the module tree
        │
        ├─ payload is .py  → run __init__.py / package entry
        │
        └─ payload is .wasm → instantiate container, enter via same
             package hook; nested packages map onto nested names
```

So:

- **`__init__.{ext}`** is the per-package enter/source file.
- **Wasm** is a package container, not a parallel universe of natives.
- **Entrypoint language** is per package via `.module` `impl`.
- **Firmware-resident** modules attach via the registered C catalog.
- Behaviour stays one mental model for “what is a module.”

Until that loader exists: human `__init__.{ext}`; explicit ready `…_init`
for bring-up; `.pyi` + register-time C→Python bind for non-Python impls.

---

## Relation to today’s tree

| Today | Direction |
|-------|-----------|
| `include/pymergetic/metal/**/*.h` handwritten | Become generated next to modules; retire dual tree |
| `runtime/mem/{mem,arena,tlsf*}.c` | Move into flat module sources (lang TBD); libc/limits **out** of mem |
| `runtime/mem/host_stubs`, `libc*.c`, `limit*` | Wrong concern — not part of the mem module |
| `exp2/` | First place to adopt `.module` + `__init__` entry + lang pool |

Product `scripts/` / EDK2 ports under `src/bios` / `src/efi` stay
dialect- and port-shaped until a module is migrated.

---

## Checklist (new module)

1. Create directory + `.pm/module` JSON (`type`, `impl`, …).
2. Add `__init__.{impl_ext}` (e.g. `__init__.rs`) and sibling sources.
3. Optional schema: `.pm/Cargo.toml`, `.pm/build.{ext}`, `.pm/smoke.{ext}`.
4. Expose a C-shaped border in that source (`extern "C"` / prototypes)
   so export can fill the in-memory catalog.
5. Run `metal mod sync` → other pool faces + module `.gitignore`
   (optional: `--emit toml`).
6. Wire build to compile human sources (+ C glue if any) and `-I src`.
7. Confirm sync refuses to overwrite human files (no ownership banner).
8. Do not emit faces from source while skipping the catalog object; do
   not regenerate human `__init__.{ext}`.
