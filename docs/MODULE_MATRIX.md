# Module matrix — export faces × async compliance

**Source of truth** for seat coverage under `pymergetic.metal.*`.
Keep this file updated when a seat lands, shrinks, or grows a face.
Interactive view (optional): Cursor canvas `metal-module-matrix` — regenerate
from this table when the ledger moves.

Related: [`SOURCETREE.md`](SOURCETREE.md) (path == module) ·
[`../include/SYMBOLS.md`](../include/SYMBOLS.md) (symbol spelling) ·
[`LIBC_ASYNC.md`](LIBC_ASYNC.md) (sync vs async ABI classes).

---

## Columns

| Column | Meaning |
|--------|---------|
| **Path** | `pymergetic.metal.<path>` — must match `include|src|glue|typings/.../pymergetic/metal/<path>` |
| **Impl** | Language of the muscle (`C` / `RS` / `Py` / combos) |
| **API** | Public surface size (exported methods + module consts that callers use) |
| **C / RS / Py** | How many of **API** are available on that face → shown as `% (n/api)` in the canvas |
| **Async** | Fully async-compliant for Metal runners? `yes` · `partial` · `no` |
| **Stub** | IDE `.pyi` under `typings/` exists — **not** “crippled”. Empty stub cell = no typings file yet (Py face may still exist via glue/frozen). |
| **Browser** | Linked / nested on wasm browser image |
| **FW** | Linked on firmware boards |
| **Note** | Short seat hint (facts about today’s seat) |
| **Dev path** | Planned work. Waves below. Blank = not scheduled yet. |

**Law: 1 impl · 3 faces — always.** Muscle is exactly one of C / RS / Py.  
Callers are **any** of C / RS / Py. Missing face = **0%**, not “N/A”.

| Muscle | Faces still required |
|--------|----------------------|
| **C** or **RS** | C ABI · RS face · Py glue+`.pyi` |
| **Py** (frozen CORE / seat / app) | **C + RS bridges that call into Py** · Py import surface |
| **C ABI + Py app** (e.g. inspect) | C ABI · **RS bridge** on that ABI · Py app stays |

Bridges are faces, not a second algorithm. Pure Py does **not** mean “C/RS never call this.”

**Full export (green):** `C == API` ∧ `RS == API` ∧ `Py == API`.  
**Green row (strict):** full export **and** `Async = yes`.

Missing Py nest ⇒ `ImportError` (no stub `-1`).

---

## Hints — how to maintain this ledger

### When to edit

Update a row (or add one) whenever you:

1. Add / remove / rename a public symbol in `include/.../<path>/`
2. Add or drop a glue nest (`glue/.../<path>.c`) or `.pyi`
3. Add or drop a Rust `pub fn` face for that path
4. Wire (or unwire) parkable I/O — flip **Async**
5. Change board / wasm link sets (**Browser** / **FW**)

Do **not** leave SYMBOLS.md saying `ok` while this matrix still shows `Py 0%`.

### How to recount faces

```text
API  = size of the public surface you expect callers to use
C    = non-static exported `pm_metal_*` in include/.../<path>/ (ignore `*_inline`)
RS   = `pub fn` on the path’s Rust callee / FFI face (0 if none)
Py   = nest globals in glue/... (minus __name__)  — prefer max(glue, .pyi count)
```

Quick recount (from `extmod/metal`):

```bash
# C exports in one header tree
rg -N '^(?!static).*\bpm_metal_\w+\s*\(' include/pymergetic/metal/util/endian/__init__.h

# Py glue qstrs (rough)
rg -N 'MP_ROM_QSTR\(MP_QSTR_' glue/pymergetic/metal/util/endian.c

# RS pub fn
rg -N '^\s*pub\s+(unsafe\s+)?(extern\s+"C"\s+)?fn\s+' src/pymergetic/metal/util/lz4/
```

### Async = yes | partial | no

Aligned with [`LIBC_ASYNC.md`](LIBC_ASYNC.md): anything that waits on the world
must park; CPU work may stay sync.

| Mark | Use when |
|------|----------|
| **yes** | No hidden blocking that cannot park. Pure CPU / memory, or all waits go through Metal async handles / pump. |
| **partial** | Some paths park or poll; others still sync-block (or sync walk of unbounded data). |
| **no** | Primary I/O or sleep path blocks the runner today. |

Hints by area:

- **util** CPU codecs/tags → usually `yes`; archive walkers → `partial` until chunked/parked
- **net** → `partial` until every wait is handle-based; `net.pump` / `net.asgi` aim `yes`
- **fs** memory/eager ops → `yes` when awaits return `completed_u32` (RAM BD); archive open/walk → `partial` until chunked
- **dev.blk** → `yes` when `read_async` parks and sync `read` is only a façade
- **mem** / **async** / **rt** → `yes` (machinery itself)
- **gfx** present/blit → sync façade is OK (`yes`/`partial`); never `sleep` in the worker

### Seat checklist (new module)

- [ ] `include/.../<path>/__init__.h` — C ABI (`pm_metal_<path_underscores>_*`)
- [ ] `src/.../<path>/` — one callee lang
- [ ] Hot-path twins use `*_inline` in the header; exports wrap them
- [ ] `glue/.../<path>.c` + nest parent `__init__.c` + `modules.h` + `glue_src.mk` / wasm SRC
- [ ] `typings/.../<path>.pyi`
- [ ] Board `OBJ` + rule (and browser list if seat=all)
- [ ] Row in **this file** + [`SYMBOLS.md`](../include/SYMBOLS.md)
- [ ] Async mark honest (default `no` if unsure about I/O)
- [ ] **Dev path** filled when the approach is decided (not before)

### Waves (Dev path)

**1 impl · 3 faces always.**

| Wave | Goal | Touch Py? |
|------|------|-----------|
| **W1** | Muscle = pure one lang (`pure C` / `pure RS`). Kill hybrid / second algorithm on the path. | no |
| **W2** | **C ↔ RS** faces — first batch (small / util·auth·trust…). | no |
| **W3** | **C ↔ RS** faces — remaining pure C/RS seats (still no Py). | no |
| **W4** | **Into-Py bridges:** C/RS faces for Py-muscle seats (`+C`/`+RS` that call Py). Also RS bridge on C+Py ABI seats (e.g. inspect). | bridge only |
| **W5** | **C/RS → Py via glue only:** nested builtin `glue/.../*.c` + `.pyi` → 100% API. Also **async** → parkable. **No** new frozen `.py` wrappers for C/RS muscles. | glue + async |
| **W6** | **Freeze diet:** stop freezing files that aren’t real Py muscle. Does **not** delete inspect/microdot/arch/unix. See inventory below. | unfreeze junk |
| **W7** | **Browser seat (non-net):** **all `util.*`** + fs/mem/async/rt/hwtree/… (see `W7 browser` tags). Still skips PCI/virtio/NIC/blk/gop and host-unix. | browser |
| **W8** | **Browser net complete:** every `net.*` seat on wasm with the **same C ABI**; browser/host shim under the hood (fetch / WebSocket / WASI sockets / CDN relay — TBD). Includes ip, dns, http, tls, ssh, wg, dhcp, ntp, tftp, nic, faces, pump, asgi. Goal: `Browser=yes` on all net rows. | browser net |

Dev path spellings: `W1…W8` · `W5 +Py` · `W5 async` · `W6 unfreeze` · `W7 browser` · `W8 browser net` · `keep Py muscle`.

### Py face rule (C/RS muscles)

```text
C/RS muscle  →  Py face = glue nest + typings/.pyi   (never a frozen reexport .py)
Py muscle    →  frozen .py is correct (listed KEEP below)
```

### W6 freeze inventory (`port/manifest*.py` today)

**KEEP frozen (real Py muscle)** — do not remove:

| Manifest | Files |
|----------|--------|
| FW + wasm | `net/microdot/{__init__,microdot,helpers}.py` |
| FW + wasm | `inspect/{__init__,stubs,self_desc,adapter_microdot,app,dispatch}.py` |
| FW + wasm | `arch/__init__.py` + `arch/{x86,x86_64,wasm}/{__init__,autoexec}.py` (+ `wasm/sim.py`) |
| unix only | `unix/{__init__,__main__}.py` + `unix/{x86,x86_64}/{__init__,autoexec}.py` |
| all | µPy `asyncio` (extmod include) |

**UNFREEZE / drop from manifest (unnecessary)** — W6 target:

| File | Why |
|------|-----|
| `metal/boot/__init__.py` | One comment line. Boot UX is C (`boot.tree` + `port/boot`). Nest/`__path__` is enough if anything imports the package name. |

**Not frozen today (already correct)** — C/RS seats use glue only; no frozen `util.*` / `net.*` / `auth` / … reexports.

**Later (arch thin — not “delete arch”):** after `arch` Py calls C ABI, keep freezing seat packs + autoexec; only drop duplicated *identity* logic in `arch/__init__.py`, not the seats.

**Py-muscle seats** (W4 bridges) — `arch*` / `unix*` / `net.microdot` / `inspect` app.

### Canvas sync

After editing the table below, refresh the Cursor canvas
`metal-module-matrix.canvas.tsx` (project `canvases/`) so agents/UI match.
Doc wins on conflict.

---

## Matrix (alphabetical by path)

Counts are ledger estimates (not a live link inventory).

| Path | Impl | API | C | RS | Py | Async | Stub | Browser | FW | Note | Dev path |
|------|------|----:|--:|---:|---:|-------|:----:|:-------:|:--:|------|----------|
| `arch` | Py+C | 5 | 5 | 5 | 5 | yes | — | yes | yes | CFG seat + into-Py name/names | keep seats · W4 bridges done · W5 +Py(=glue) · W6 no dual Py |
| `arch.wasm` | Py | 3 | 3 | 3 | 3 | yes | — | yes | yes |  | keep Py muscle · W4 +C/+RS bridges |
| `arch.x86` | Py | 3 | 3 | 3 | 3 | yes | — | yes | yes |  | keep Py muscle · W4 +C/+RS bridges |
| `arch.x86_64` | Py | 3 | 3 | 3 | 3 | yes | — | yes | yes |  | keep Py muscle · W4 +C/+RS bridges |
| `async` | C | 12 | 12 | 12 | 12 | yes | — | — | yes |  | W1 pure C · W5 +Py · W7 browser |
| `auth` | C | 9 | 9 | 9 | 9 | yes | yes | yes | yes |  | done |
| `boot` | C | 4 | 4 | 4 | 4 | yes | yes | yes | yes | thin face over boot.tree UX | W6 unfreeze done |
| `boot.tree` | C | 11 | 11 | 11 | 11 | yes | — | yes | yes |  | pure C · W5 +Py |
| `bus.pci` | C | 8 | 8 | 8 | 8 | yes | — | — | yes |  | W5 +Py |
| `bus.virtio` | C | 15 | 15 | 15 | 15 | yes | — | — | yes |  | done |
| `console` | C | 15 | 15 | 15 | 15 | yes | — | yes | yes | sync ring façade | done |
| `dev.acpi` | C | 6 | 6 | 6 | 6 | yes | — | — | yes |  | W5 +Py |
| `dev.blk` | C | 6 | 6 | 6 | 6 | yes | — | — | — | read_async parks; read façade | done |
| `dev.gfx.compositor` | C | 10 | 10 | 10 | 10 | yes | — | — | yes | sync present façade | done |
| `dev.gfx.scanout` | C | 8 | 8 | 8 | 8 | partial | — | — | yes | virtio-gpu/bochs/radeon/i915/GOP/LFB | W5 +Py · async |
| `dev.gfx.text` | C | 3 | 3 | 3 | 3 | yes | — | — | yes |  | W5 +Py |
| `dev.input.kbd` | C | 5 | 5 | 5 | 5 | yes | — | — | yes |  | W5 +Py |
| `dev.net.bge` | C | 6 | 6 | 6 | 6 | yes | — | — | yes | L2 poll façade | done |
| `dev.net.virtio_net` | C | 7 | 7 | 7 | 7 | yes | — | — | yes | L2 poll façade | done |
| `dev.serial` | C | 2 | 2 | 2 | 2 | yes | yes | — | yes | write sink | done |
| `dev.stream` | C | 18 | 18 | 18 | 18 | yes | — | — | — | park read/drain | done |
| `draw` | C | 6 | 6 | 6 | 6 | yes | — | — | yes |  | W5 +Py |
| `externals` | C | 6 | 6 | 6 | 6 | yes | yes | yes | yes |  | done |
| `fs` | RS | 24 | 24 | 24 | 24 | yes | yes | — | — | *_async border; eager completed_u32 | done · W7 browser |
| `fs.embed` | RS | 2 | 2 | 2 | 2 | yes | yes | — | — | embed_c / embed_rs | W7 browser |
| `fs.fat` | RS | 6 | 6 | 6 | 6 | yes | yes | — | — | in-RAM FAT; completed_u32 | done · W7 browser |
| `fs.littlefs` | RS | 1 | 1 | 1 | 1 | yes | yes | — | — | in-RAM BD; completed_u32 | done · W7 browser |
| `fs.mtar` | RS | 6 | 6 | 6 | 6 | no | yes | — | — | open walks tar | async · W7 browser |
| `fs.overlay` | RS | 1 | 1 | 1 | 1 | yes | yes | — | — | forwarder only | done · W7 browser |
| `fs.tmpfs` | RS | 1 | 1 | 1 | 1 | yes | yes | — | — | memory-backed | W7 browser |
| `fs.vfs` | RS | 6 | 6 | 6 | 6 | yes | yes | — | — | mount table only | done · W7 browser |
| `fs.wasmmod` | RS | 1 | 1 | 1 | 1 | yes | yes | — | — | RO memory MPWP | done · W7 browser |
| `fs.zip` | RS | 4 | 4 | 4 | 4 | no | yes | — | — | open scans CD/EOCD | async · W7 browser |
| `hwtree` | RS | 1 | 1 | 1 | 1 | yes | yes | — | — | print only (DT walk) | W7 browser |
| `inspect` | Py+C | 6 | 6 | 6 | 6 | yes | — | yes | yes | C+RS into-Py via pm_upy | keep Py app · W4 bridges done |
| `mem.arena` | RS | 13 | 13 | 13 | 13 | yes | yes | — | — | Arena bytearray face | W7 browser |
| `mem.lock` | RS | 8 | 8 | 8 | 8 | yes | yes | — | — | spin+mutex word buffers | W7 browser |
| `mem.port` | C | 4 | 4 | 4 | 4 | yes | yes | yes | yes |  | done |
| `mem.tlsf` | RS | 20 | 20 | 20 | 20 | yes | yes | yes | yes | Conte TLSF border | done |
| `net.microdot` | Py | 20 | 20 | 20 | 20 | partial | — | yes | yes | into-Py resolve/new + route/run/get/post + getattr/call* | keep Py muscle · W5 async |
| `net.asgi` | C | 4 | 4 | 4 | 4 | yes | — | — | yes | Py is consumer/codegen only | pure C · W5 +Py · W8 browser net |
| `net.dhcp` | C | 4 | 4 | 4 | 4 | yes | — | — | yes | start parks; run sync façade | done · W8 browser net |
| `net.dns` | C | 3 | 3 | 3 | 3 | yes | — | — | yes | lookup→ip_dns_lookup; resolve façade | done · W8 browser net |
| `net.faces` | C | 3 | 3 | 3 | 3 | yes | yes | — | yes |  | W8 browser net |
| `net.http` | C | 12 | 12 | 12 | 12 | yes | — | — | yes | get parks; client_get façade | done · W8 browser net |
| `net.ip` | C | 13 | 13 | 13 | 13 | yes | yes | — | yes | socks+dns_lookup park | done · W8 browser net |
| `net.nic` | C | 4 | 4 | 4 | 4 | yes | — | — | yes | register+L2 poll | done · W8 browser net |
| `net.ntp` | C | 7 | 7 | 7 | 7 | yes | — | — | yes | sync parks; query* façades | done · W8 browser net |
| `net.pump` | C | 7 | 7 | 7 | 7 | yes | — | — | yes |  | W5 +Py · W8 browser net |
| `net.ssh` | C | 16 | 16 | 16 | 16 | yes | yes | — | yes | server poll-driven; client_exec stub | done · W8 browser net · client later |
| `net.tftp` | C | 6 | 6 | 6 | 6 | yes | — | — | yes | get_async parks; get façade | done · W8 browser net |
| `net.tls` | C | 18 | 18 | 18 | 18 | yes | — | — | yes | handshake parks; load_ca_file façade | done · W8 browser net |
| `net.wg` | C | 12 | 12 | 12 | 12 | yes | yes | — | yes | up/peer sync CPU; handshake_smoke façade | done · W8 browser net |
| `pack` | C | 6 | 6 | 6 | 6 | yes | — | yes | yes |  | W5 +Py |
| `rt` | RS | 5 | 5 | 5 | 5 | yes | yes | — | yes | halt/panic*/register/connect | W7 browser |
| `shell.tui` | C | 4 | 4 | 4 | 4 | yes | — | — | yes |  | done |
| `shell.ui` | C | 2 | 2 | 2 | 2 | yes | — | — | yes |  | done |
| `shell.vt` | C | 9 | 9 | 9 | 9 | yes | — | — | yes |  | done |
| `trust` | C | 7 | 7 | 7 | 7 | yes | yes | yes | yes |  | done |
| `unix.x86` | Py | 2 | 2 | 2 | 2 | no | — | — | — | host sim sync | keep Py muscle · W4 +C/+RS bridges · W5 async |
| `unix.x86_64` | Py | 2 | 2 | 2 | 2 | no | — | — | — | host sim sync | keep Py muscle · W4 +C/+RS bridges · W5 async |
| `util.ascii` | C | 5 | 5 | 5 | 5 | yes | yes | yes | yes |  | W7 browser |
| `util.eightcc` | C | 9 | 9 | 9 | 9 | yes | yes | yes | yes |  | W7 browser |
| `util.endian` | C | 7 | 7 | 7 | 7 | yes | yes | yes | yes | `*_inline`; +WIRE_IS_LE on Py | W7 browser |
| `util.fourcc` | C | 9 | 9 | 9 | 9 | yes | yes | yes | yes |  | W7 browser |
| `util.lz4` | RS | 3 | 3 | 3 | 3 | yes | yes | yes | yes |  | W1 pure RS · W7 browser |
| `util.size` | RS | 2 | 2 | 2 | 2 | yes | yes | yes | yes |  | W1 pure RS · W7 browser |
| `util.tar` | RS | 5 | 5 | 5 | 5 | partial | yes | — | yes | sync walk today | async · W7 browser |
| `wamr_host` | RS | 15 | 15 | 15 | 15 | partial | yes | — | yes | LINK_WAMR=1; guest may block | async |

---

## Snapshot (as of last edit)

| Metric | Value |
|--------|------:|
| Rows | 69 |
| Full export (C∧RS∧Py @ 100%) | **69/69** |
| Strict green (export ∧ async=yes) | **61/69** |
| Smoke | `X86_64_BIOS_OK` ENGINE=mp (2026-08-09; fs/nic/blk + net async façades) |
| Note | Product link uses `abi_faces_link.c` for seats not yet in RUST_LIBS; Py = max(glue, .pyi). |

Recompute the snapshot numbers when you bulk-edit the table.
