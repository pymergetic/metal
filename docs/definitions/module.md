# Definitions — polyglot module

How a Metal **module** is laid out so any of C / C++ / Rust / Python can
be the implementation language, while every language consumes one
unified public surface. Experimentation lives under ``; product
`src/` / `include/` migrate toward this over time.

**Tooling / CLI workflow:** [`docs/TOOLING.md`](../TOOLING.md) (`metal`
under `tools/metal/`).

**Status — corrected three times; see the `registration_rethink_scope`
plan's "Phase C/D re-correction" section for the full history and the
canonical boot order.** Two prior passes each overcorrected the other's
mistake:

1. Pass 1 applied the runtime registry (cached fn pointer, refcount) to
   **every** compile-time-known floor module unconditionally, including
   ones never unloaded (`console`, `mem`, `log`, `async`, `boot`, the
   detector modules, ...).
2. Pass 2 "fixed" that by reading "unloadable vs. fixed needs different
   ref handling, so we have a fast path" as license to drop the
   proxy/cache **entirely** for fixed modules, down to plain Cargo-
   dependency direct calls — no cache slot, no runtime connect, nothing
   going through the registry at all for a fixed peer.
3. This session repeated pass 2's mistake in the other direction: it
   restored a real registry-proxy (cached slot, `connect_symbols`,
   refcount) for **unloadable** providers, but *still* left fixed
   providers on pass 2's plain-`extern "C"`-link-time shape instead of
   giving them a proxy too.

**The actual rule (locked in the registration_rethink design's own
"Registry design verdict" table, which none of the three passes above
actually contradicted — they just didn't read it): every cross-module
call, fixed or unloadable provider alike, goes through a proxy that
loads a cached pointer and calls through it — "Proxies even for
mod-internal calls -> Yes -- only mem + reg are direct spine." Refcount
is the *only* thing that forks on unloadable vs. fixed, not whether a
proxy/cache exists at all:**

- **Every module's public surface is always auto-generated** into
  `include/pymergetic/metal/<mod>/...` (mirrored from `_impl/`, the
  `_impl` segment and leading underscores stripped) for every consumer
  language, Rust-to-Rust included. A module's own `src/` directory holds
  only human input (`_impl/`, `.pm/`) — never a generated face. The only
  exception is the **spine**: `mem` and `reg` are foundational/bootstrap-
  critical enough that a direct `_impl` Cargo dependency on them is
  tolerated everywhere.
- **Every generated face is a proxy**, resolved once by `connect_symbols`
  (proactively, right after any load/unload — not a lazy per-call check,
  so the hot path is a pure "load slot, call," no branch): a fixed
  provider's slot is written once and never touched again (no lock, no
  refcount needed because it cannot disappear); an unloadable provider's
  slot is refcounted/handled so it can't be called through mid-unload,
  and unload has to disconnect every connected importer via a two-sided
  handshake (either side may initiate; the other side accepts) rather
  than being reached into from outside.
- A genuine Cargo cycle (e.g. `rt` <-> `console`/`mem`, both of which
  depend on `rt`) still resolves via a raw `extern "C"` forward
  declaration since `rt` cannot Cargo-depend on either — this remains
  the one deliberate exception (`rt/_impl/_ffi.rs`), not a template for
  every fixed provider.

**Not yet implemented as of this correction** (tracked in the plan's
"Phase C/D re-correction" open items): the always-proxy shape for fixed
providers (today's tree still has pass 2/3's plain-Cargo/fast-path faces
for the 14 migrated floor modules), the two-sided disconnect handshake,
and re-pointing `wasm`'s loader at the same `RegMod`/`RegEntry`
mechanism instead of `reg`'s separate dynamic `_table.rs`. Same-module-
internal calls (between a module's own `_impl/*.rs` files) stay direct
either way, unaffected by any of this.

**Kernel is a module too.** The `pymergetic.metal` namespace root
(`src/pymergetic/metal/.pm/module`) is the first module in boot order,
permanently `unloadable = false`, and follows the exact same
register/connect/lifecycle shape as every other module — not a bare
marker with zero ABI. See "Module lifecycle" below.

---

## Goals

- **One module folder** under `src/…`, human-only: `_impl/` (real
  sources) + `.pm/` (metadata) — nothing else. Every generated face for
  that module mirrors out to `include/pymergetic/metal/<mod>/…`, never
  colocated with the human source.
- **Any language may implement**; any language may consume, always
  through the generated `include/` face (spine exception: `mem`, `reg`).
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
  _impl/                          # HUMAN — the whole implementation
    __init__.rs                   # package entry when impl=rs
    …                             # other HUMAN sibling stems (same lang)
  .pm/
    module                        # HUMAN — JSON metadata (required)
    Cargo.toml                    # HUMAN — Rust crate (when impl=rs)
    build.rs / smoke.*            # optional schema
  nested/                         # optional nested Metal module
    .pm/module
    _impl/__init__.rs
  platform/                       # example: nested C module (boot ops)
    .pm/module                    # impl=c
    _impl/*.h                     # HUMAN C stems
    bios/ efi/                    # hidden ports (.pm/module type=hidden)

include/pymergetic/metal/<mod>/   # GENERATED ONLY — every pool face for
  __init__.h __init__.pyi …       # every module lands here, mirrored from
                                   # _impl/ (that path segment dropped)
```

| Path | Who writes it |
|------|----------------|
| `.pm/module` | human (JSON) |
| `.pm/Cargo.toml`, `.pm/build.*`, `.pm/smoke.*` | human |
| `_impl/__init__.{impl_ext}` and other impl sources | human |
| `type=hidden` trees | human — never codegen |
| `include/pymergetic/metal/<mod>/*` (`*.h` / `*.rs` / `*.pyi` / optional `*.toml`) | **codegen** (banner-owned) |

**A module's own directory under `src/` holds only `_impl/` and `.pm/` —
nothing else.** Every generated face, for every pool language including
the module's own consumers, lives under the separate `include/` tree,
never colocated with human source. This is what keeps `_impl/` "safe to
touch" — it is never itself a build/include root for anyone outside the
module, so renames/refactors inside it can never break a foreign caller
by accident; the only contract a foreign caller sees is the generated
face.

### Banner write gate (hard)

Before writing output `REL` (path relative to `include/pymergetic/metal/`):

1. The module at the mirrored `src/` location has `.pm/module` with
   `type` `module` or `package`.
2. `REL`'s mirrored `_impl/` source is not under `.pm/` and not under any
   `type=hidden` subtree.
3. If `include/pymergetic/metal/REL` **exists** and does **not** contain
   `DO NOT HAND-EDIT THIS FILE.` → **refuse** (treat as human-owned; this
   should never actually happen since nothing hand-writes into `include/`).
4. Otherwise write `REL` (content must include that banner line).

**Entry symmetry:** package entry is usually `__init__` + `impl` extension
(for `impl=c`, prefer `_impl/__init__.h` then `_impl/__init__.c`).
Sibling-only modules are allowed when every public stem is a sibling
(e.g. `boot/platform/` with `uart.h`, `mem_map.h`, … and no umbrella
`__init__.h`). Generated faces are the **other** pool extensions of each
stem, plus (per "Module lifecycle" below) either a fast-path `extern "C"`
declaration or a registry-proxy, depending on the provider's
`unloadable` flag. Sync never emits the impl's own slot (no second
`uart.h` when `impl=c`).

### `include/` is wholesale-generated (no per-file bookkeeping)

The entire `include/` tree is generated and gitignored with a single
root-level `/include/` line — there is no per-module `.gitignore`
management for generated faces anymore (a module's own directory never
contains anything generated to ignore). `metal mod clean` wipes and lets
the next `mod sync` regenerate the whole tree; there is no partial/stale
state to reconcile file-by-file.

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
              EMIT every pool slot, own included
                         │
         ┌───────┬───────┼───────┐
         ▼       ▼       ▼       ▼
        c       rs      py     toml
      *.h    *.rs    *.pyi  *.toml

  Never: any face written back over the human {base}.{impl_ext} itself
  (own-slot output always lands in include/, never src/).
```

| Pool slot | Default emit | Can be impl? | Face |
|-----------|--------------|--------------|------|
| `c` | yes | yes (`impl=c` or `impl=cpp`) | `{base}.h` |
| `rs` | yes | yes | `{base}.rs` |
| `py` | yes | yes | `{base}.pyi` |
| `toml` | yes | **no** | `{base}.toml` |

**Every** pool slot is emitted, including the impl's own language:
`impl=rs` → emit `c`+`rs`+`py`(+`toml`); `impl=c`/`cpp` → emit
`c`+`rs`+`py`(+`toml`); etc. "Input in language X, output in all
languages" — no exceptions, no same-language free pass. The own-slot
output is a real generated mirror under `include/`, distinct from the
human source under `src/`; a same-language consumer still goes through
that generated mirror like everyone else (see "Consume foreign modules"
below), not a shortcut back to `_impl/`. `toml` is a full catalog dump
(structs/enums/typedefs/fns, `inline` included) useful as a debug hint —
"what did the importer actually detect" — when a face looks off; it is
never an impl and is not compared to the other faces' border-only name
set (`metal mod check` excludes it from symmetry). Stale marker-owned
faces not in this run's emit set are pruned.

### Border vs interior

When the `c` slot is emitted, that `{base}.h` is the **runtime border**:
`extern "C"`, `pm_metal_…`. C++ consumers just `#include` it — no separate
C++ wrapper face. Interior of the module may use Rust `api::`, C++
templates, etc.

### Consume foreign modules (no ABI twins)

Call another module through **its** generated `include/pymergetic/metal/<mod>/`
face for your language — always, including Rust-to-Rust. Do not
redeclare its structs / `extern "C"` / private twin headers, and do not
Cargo-depend on a peer's `_impl` crate just to call it directly (that
skips the generated face and re-creates exactly the "hand-duplicated
ABI" this rule exists to prevent). The two exceptions:

- **Spine:** `mem` and `reg` are foundational/bootstrap-critical enough
  that every consumer takes a direct `_impl` Cargo dependency on them and
  calls straight through — no generated face, no indirection at all
  (matches "Call path: proxies everywhere except spine" in the
  `registration_rethink` design).
- **A genuine Cargo cycle** (e.g. `rt` <-> `console`/`mem`, both of which
  depend on `rt`): a raw `extern "C"` forward declaration, hand-written,
  since neither side can Cargo-depend on the other. This is the same
  shape a generated fast-path face would produce; it's just hand-written
  here because generating it would require the provider to depend on the
  consumer to know its own name, which is what the cycle forbids.

| Provider `impl` | C consumer | Rust consumer |
|-----------------|------------|---------------|
| `rs` / `py` | generated `{base}.h` | generated `{base}.rs` (Rust-to-Rust included) |
| `c` / `cpp` | generated `{base}.h` (own-language mirror) | generated `{base}.rs` |

The provider's own human source (`{base}.h` for `impl=c`/`cpp`, `{base}.rs`
for `impl=rs`) is never the thing another module includes/uses — every
consumer, same-language or not, goes through the generated `include/`
face.

Example: `boot`'s Rust bring-up code consumes `boot/platform`'s (impl=c)
UART ops via `#[path = "../../../../../include/pymergetic/metal/boot/platform/__init__.rs"]`
(generated from human `uart.h`) — never a colocated `platform/uart.rs`
twin, per "One module folder" above. BIOS C includes human
`platform/*.h` and generated `include/pymergetic/metal/mem/__init__.h` /
`include/pymergetic/metal/dt/__init__.h`. Safe wrappers may wrap the
face; they must not copy it.

### Two face shapes: fast-path vs registry-proxy

What a generated face actually contains depends on the **provider's**
`unloadable` flag (see Markers below), not the consumer:

| Provider `unloadable` | Face shape | Runtime cost |
|------------------------|-----------|---------------|
| `false` (permanently linked, or a `package` marked sticky) | Fast path: plain `extern "C"` declaration | Link-time resolution only — no cache slot, no `connect_symbols` participation, no refcount |
| `true` (genuinely reloadable/unloadable — wasm, Python) | Registry proxy: cached slot populated by `connect_symbols`, refcounted on call | One indirect load + a refcount inc/dec per call |

This is the only place unloadability matters. Whether a face exists at
all is unconditional (every module, every consumer language); which
shape it takes is conditional on the provider.

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
| `{base}.h` | always |
| `{base}.rs` | always |
| `{base}.pyi` | always |
| `{base}.toml` | always (debug dump; excluded from `mod check` symmetry) |
| Python runtime bind | later — register/attach C→Python when `impl != py` |

**Dep budget:** `re`, basic containers, tiny hand parsers.

---

## Markers — `.pm/module` + `type`

One JSON file per directory: `.pm/module`. The former root files
(`.module` / `.package` / `.nomodule`) are obsolete.

| `type` | Role |
|---------------|------|
| `module` | **Kernel module** — sync/codegen; firmware link when applicable. |
| `package` | **Wasm pack** — forge → `.wasm` from **C or Rust**; not kernel-linked. Loaded by `wasm/`; **one Metal memory**. See [`ORCHESTRATION.md`](../ORCHESTRATION.md) Locked #7–#8. First proofs under package-root `tests/`. |
| `hidden` | **Forbid** — plain port or namespace shell; tools refuse codegen. |

```json
{
  "type": "module",
  "name": "pymergetic.metal.dt",
  "impl": "rs",
  "version": "0.1.0",
  "unloadable": false
}
```

**`unloadable` (bool, optional):** can this provider be reloaded/unloaded
without every consumer's Cargo graph changing? Defaults from `type` when
omitted: `module` -> always `false` (kernel-linked, permanent); `package`
-> `true` (a wasm pack, assumed reloadable). A `package` may set
`"unloadable": false` explicitly to opt into the fast-path face despite
being wasm — the "sticky" case (e.g. a pack that's always resident once
loaded and never actually unloaded in practice). `module` may not set
`"unloadable": true` — a kernel-linked module cannot be unloaded, full
stop. See "Two face shapes" above and "Module lifecycle" below.

`hidden` may be only `{ "type": "hidden" }`. Rust crates also keep
`Cargo.toml` (and optional `build` / `smoke`) under `.pm/`.

Public pool faces are emitted from the in-memory catalog — never
hand-written without the ownership banner. `{base}.toml` is emitted on
every sync too (debug dump of the catalog).

**Tool checks before writing into `DIR`:**

1. `DIR/.pm/module` exists with `type` `module` or `package`.
2. Output path passes the banner write gate above (never under `.pm/` or
   a `hidden` subtree).
3. Prefer known pool face basenames (`{base}.h`, `{base}.rs`,
   `{base}.pyi`, `{base}.toml`).

---

## Module lifecycle

Every module — including the kernel namespace root itself — follows the
same six-hook shape (full design/rationale:
[`registration_rethink_scope_5d2d2f59.plan.md`](../../.cursor/plans/registration_rethink_scope_5d2d2f59.plan.md)
"Bootstrap (refined)"):

| Hook | Who calls it | Does |
|------|--------------|------|
| `on_load` | loader (boot, or a wasm/py loader later) | one-time setup before registering exports |
| `register_symbols` | loading module | publish this module's exports |
| `connect_symbols` | every loaded module, after any load/unload | resolve this module's imports against currently-registered exports |
| `on_registrations_updated` | every loaded module | optional extra hook; default just calls `connect_symbols` again |
| `deregister_symbols` | unloading module | withdraw this module's exports before teardown |
| `on_unload` | unloading module | release resources; **cascades to child module substructure first** (a package's children are unloaded before the package itself finishes unloading) |

**Kernel = first module.** `pymergetic.metal`'s own `_impl/__init__.rs`
runs `on_load` -> `register_symbols` -> `connect_symbols` first, before
any other module, and is permanently `unloadable = false` — it never runs
`deregister_symbols`/`on_unload` in practice, but it carries the same
hook shape as everything else ("kernel is kernel": permanent and first,
not architecturally special).

**Refcount only crosses an unloadable boundary.** A cross-module call
where *either* side is `unloadable = true` goes through a refcounted
handle (increment on acquire, decrement on release, entry stays live
until refs hit zero even if unload is requested mid-call). When *both*
sides are permanent (`unloadable = false`, including a sticky package),
the fast-path face applies and no refcount/lock exists on that path at
all — see "Two face shapes" above.

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

**Directory = Metal module** (`.pm/module`). Nested packages are nested
dirs with their own `.module` + `__init__.{ext}` (e.g.
`mem/tlsf/__init__.rs`). **Only directories are modules** — a lone source
file is never a module; it is a **sibling stem** inside a module dir.

**Sibling sources** in the same dir are extra stems sync may codegen
when they carry a pool border. Prefer nested dirs with their own
`.module` + `__init__.{ext}` for real subpackages (`mem/arena/`,
`mem/tlsf/`). Vendored C stays external (FFI/shim).

`forge mod sync` table uses **Python-style dotted names** and a `kind`
column:

| kind | Meaning |
|------|---------|
| `d` | Package entry (`__init__`) of a module directory |
| `f` | Sibling stem in that directory (e.g. `await`, `mutex`) |

Example: `pymergetic.metal.async` (`d`) vs
`pymergetic.metal.async.await` (`f`).

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

[`src/pymergetic/metal/forge/`](../../src/pymergetic/metal/forge/)
is a normal Metal module (`impl=rs`, lang-pool faces on `__init__`) that
*implements* module sync/check. Per pool language: `_import_{c,rs,py,toml}`
(source → catalog) and `_export_{c,rs,py,toml}` (catalog → face); stubs
allowed until wired. `_gitignore` owns the managed ignore block.
`_port/{solo,metal}` implement **`ForgeStore`** / **`ForgeSession`**.
Ops return **`ForgePoll`** (`Ready` / `Pending`); solo is always-Ready.

| Face | Port | Hosting |
|------|------|---------|
| Outside | `_port/solo` shim only | Hidden [`forge/cli/`](../../src/pymergetic/metal/forge/cli/) + [`./forge-cli`](../../forge-cli) launcher |
| Inside (later) | `_port/metal` | Metal command → `fn_process` + console attach |

Prebuild faces with solo forge; later the binary can keep human stems only
and emit other pool faces on demand. Forge is **not** linked into EFI by
default. Archived Python metal lives under `_old/tools/metal`.

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
…/mem/_impl/__init__.rs               # HUMAN when impl=rs
…/mem/.pm/module                      # JSON metadata
…/mem/.pm/Cargo.toml                  # Rust crate
…/mem/.pm/build.rs                    # schema: cargo/native build hook
…/mem/.pm/smoke.rs                    # schema: host smoke (metal mod test)
…/mem/arena/_impl/__init__.rs         # nested module
…/mem/tlsf/_impl/__init__.rs          # nested module

include/pymergetic/metal/mem/__init__.h   # GENERATED C hub (mirrors mem/_impl/__init__.rs)
```

Same `__init__` stem throughout. When `impl=rs`, there is no generated
`__init__.rs` face for that module's *own* consumers of other languages,
but foreign Rust consumers still get a generated `include/.../__init__.rs`
face (see "Two face shapes").

---

## Build / consume sketch

```text
.pm/module + _impl/__init__.{ext}
        │
        ├─ metal mod sync
        │     1) EXPORT  impl → include/…/__init__.h  (+ C glue if needed)
        │     2) IMPORT  include/…/__init__.h → missing faces (.rs / .pyi / …)
        │        (+ ownership banner; include/ is wholesale-gitignored)
        │
        └─ compile human sources (+ any generated C glue), each consumer
           `#include`/imports the peer's `include/pymergetic/metal/<mod>/`
           face → link
```

Guest / mod builds: `-I include -I src` (source for the module's own
`_impl/`, `include` for every foreign face it consumes).

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
| `include/pymergetic/metal/**/*.h` handwritten | Retired as a *handwritten* tree; revived as a **fully generated** one (see Layout) — every module's faces (own pool faces + foreign-consumer faces, fast-path or registry-proxy per the provider's `unloadable` flag) land under `include/pymergetic/metal/<mod>/`, never hand-written, never colocated with `_impl/`. |
| `runtime/mem/{mem,arena,tlsf*}.c` | Move into flat module sources (lang TBD); libc/limits **out** of mem |
| `runtime/mem/host_stubs`, `libc*.c`, `limit*` | Wrong concern — not part of the mem module |
| `` | First place to adopt `.module` + `__init__` entry + lang pool |

Product `scripts/` / EDK2 ports under `src/bios` / `src/efi` stay
dialect- and port-shaped until a module is migrated.

---

## Checklist (new module)

1. Create directory + `.pm/module` JSON (`type`, `impl`, `unloadable`, …).
2. Add `_impl/__init__.{impl_ext}` (e.g. `_impl/__init__.rs`) and sibling
   sources — nothing else at the module root besides `_impl/` and `.pm/`.
3. Optional schema: `.pm/Cargo.toml`, `.pm/build.{ext}`, `.pm/smoke.{ext}`.
4. Expose a C-shaped border in that source (`extern "C"` / prototypes)
   so export can fill the in-memory catalog.
5. Run `metal mod sync` → every pool face (own language included, plus
   `{base}.toml`) lands under `include/pymergetic/metal/<mod>/`.
6. Wire build to compile `_impl/` sources (+ C glue if any); `-I include`
   for anything this module consumes from a peer, `-I src` for its own
   `_impl/`.
7. Confirm sync refuses to overwrite human files (no ownership banner).
8. Do not emit faces from source while skipping the catalog object; do
   not regenerate human `_impl/__init__.{ext}`.
