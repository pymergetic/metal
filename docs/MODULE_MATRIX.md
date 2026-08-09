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
- **fs** / **dev.blk** → `no` until async read/write is the only data path ([LIBC_ASYNC](LIBC_ASYNC.md) file class)
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
| FW + wasm | `microdot/{__init__,microdot,helpers}.py` |
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

**Py-muscle seats** (W4 bridges) — `arch*` / `unix*` / `microdot` / `inspect` app.

### Canvas sync

After editing the table below, refresh the Cursor canvas
`metal-module-matrix.canvas.tsx` (project `canvases/`) so agents/UI match.
Doc wins on conflict.

---

## Matrix (alphabetical by path)

Counts are ledger estimates (not a live link inventory).

| Path | Impl | API | C | RS | Py | Async | Stub | Browser | FW | Note | Dev path |
|------|------|----:|--:|---:|---:|-------|:----:|:-------:|:--:|------|----------|
| `arch` | Py+C | 4 | 1 | 0 | 4 | yes | — | yes | yes |  | thin Py → C ABI · W4 +RS bridge · W5 +Py(=glue) · W6 no dual Py |
| `arch.wasm` | Py | 3 | 0 | 0 | 3 | yes | — | yes | yes |  | keep Py muscle · W4 +C/+RS bridges |
| `arch.x86` | Py | 3 | 0 | 0 | 3 | yes | — | yes | yes |  | keep Py muscle · W4 +C/+RS bridges |
| `arch.x86_64` | Py | 3 | 0 | 0 | 3 | yes | — | yes | yes |  | keep Py muscle · W4 +C/+RS bridges |
| `async` | C | 12 | 6 | 6 | 0 | yes | — | — | yes |  | W1 pure C · W2 +RS · W5 +Py · W7 browser |
| `auth` | C | 9 | 9 | 0 | 2 | yes | yes | yes | yes |  | W2 +RS · W5 +Py |
| `boot` | C | 4 | 0 | 0 | 4 | partial | — | yes | yes | comment-only __init__ | pure C · W6 unfreeze boot/__init__.py |
| `boot.tree` | C | 11 | 11 | 0 | 0 | yes | — | yes | yes |  | pure C · W3 +RS · W5 +Py |
| `bus.pci` | C | 8 | 8 | 0 | 0 | yes | — | — | yes |  | W3 +RS · W5 +Py |
| `bus.virtio` | C | 14 | 14 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async |
| `console` | C | 15 | 15 | 0 | 0 | partial | — | yes | yes |  | W3 +RS · W5 +Py · async |
| `dev.acpi` | C | 6 | 6 | 0 | 0 | yes | — | — | yes |  | W3 +RS · W5 +Py |
| `dev.blk` | C | 8 | 4 | 4 | 0 | no | — | — | — |  | W1 pure C · W2 +RS · W5 +Py · async |
| `dev.gfx.compositor` | C | 4 | 4 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async |
| `dev.gfx.scanout` | C | 5 | 5 | 0 | 0 | partial | — | — | yes | virtio-gpu/bochs/radeon/i915/GOP/LFB | W3 +RS · W5 +Py · async |
| `dev.gfx.text` | C | 4 | 4 | 0 | 0 | yes | — | — | yes |  | W3 +RS · W5 +Py |
| `dev.input.kbd` | C | 5 | 5 | 0 | 0 | yes | — | — | yes |  | W3 +RS · W5 +Py |
| `dev.net.bge` | C | 4 | 4 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async |
| `dev.net.virtio_net` | C | 6 | 6 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async |
| `dev.serial` | C | 2 | 2 | 0 | 0 | partial | — | — | yes |  | W2 +RS · W5 +Py · async |
| `dev.stream` | C | 4 | 4 | 0 | 0 | partial | — | — | — |  | W3 +RS · W5 +Py · async |
| `draw` | C | 6 | 6 | 0 | 0 | yes | — | — | yes |  | W3 +RS · W5 +Py |
| `externals` | C | 7 | 7 | 0 | 4 | yes | yes | yes | yes |  | W2 +RS · W5 +Py |
| `fs` | RS | 22 | 9 | 22 | 0 | no | — | — | — |  | W3 +C · W5 +Py · async · W7 browser |
| `fs.embed` | RS | 3 | 0 | 3 | 0 | yes | — | — | — |  | W3 +C · W5 +Py · W7 browser |
| `fs.fat` | RS | 6 | 0 | 6 | 0 | no | — | — | — |  | W3 +C · W5 +Py · async · W7 browser |
| `fs.littlefs` | RS | 1 | 0 | 1 | 0 | no | — | — | — |  | W1 pure RS · W2 +C · W5 +Py · async · W7 browser |
| `fs.mtar` | RS | 6 | 0 | 6 | 0 | no | — | — | — |  | W3 +C · W5 +Py · async · W7 browser |
| `fs.overlay` | RS | 1 | 0 | 1 | 0 | partial | — | — | — |  | W3 +C · W5 +Py · async · W7 browser |
| `fs.tmpfs` | RS | 1 | 0 | 1 | 0 | yes | — | — | — | memory-backed | W3 +C · W5 +Py · W7 browser |
| `fs.vfs` | RS | 6 | 0 | 6 | 0 | no | — | — | — |  | W3 +C · W5 +Py · async · W7 browser |
| `fs.wasmmod` | RS | 1 | 0 | 1 | 0 | partial | — | — | — |  | W3 +C · W5 +Py · async · W7 browser |
| `fs.zip` | RS | 4 | 0 | 4 | 0 | no | — | — | — |  | W3 +C · W5 +Py · async · W7 browser |
| `hwtree` | RS | 8 | 0 | 8 | 0 | yes | — | — | — |  | W3 +C · W5 +Py · W7 browser |
| `inspect` | Py+C | 6 | 3 | 0 | 6 | yes | — | yes | yes |  | keep Py app · C ABI · W4 +RS bridge |
| `mem.arena` | RS | 24 | 0 | 24 | 0 | yes | — | — | — |  | W3 +C · W5 +Py · W7 browser |
| `mem.lock` | RS | 13 | 0 | 13 | 0 | yes | — | — | — |  | W3 +C · W5 +Py · W7 browser |
| `mem.port` | C | 4 | 4 | 0 | 0 | yes | — | yes | yes |  | W3 +RS · W5 +Py |
| `mem.tlsf` | RS | 32 | 8 | 32 | 0 | yes | — | yes | yes |  | W1 pure RS · W2 +C · W5 +Py |
| `microdot` | Py | 20 | 0 | 0 | 20 | partial | — | yes | yes | vendored CORE | keep Py muscle · W4 +C/+RS bridges · W5 async |
| `net.asgi` | C | 4 | 4 | 0 | 0 | yes | — | — | yes | Py is consumer/codegen only | pure C · W3 +RS · W5 +Py · W8 browser net |
| `net.dhcp` | C | 1 | 1 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async · W8 browser net |
| `net.dns` | C | 1 | 1 | 0 | 0 | no | — | — | yes |  | W3 +RS · W5 +Py · async · W8 browser net |
| `net.faces` | C | 3 | 3 | 0 | 0 | yes | — | — | yes |  | W2 +RS · W5 +Py · W8 browser net |
| `net.http` | C | 11 | 11 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async · W8 browser net |
| `net.ip` | C | 13 | 13 | 0 | 4 | partial | yes | — | yes | RS FFI partial / uncounted; W8: browser shim under ABI | W3 +RS · W5 +Py · async · W8 browser net |
| `net.nic` | C | 2 | 2 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async · W8 browser net |
| `net.ntp` | C | 2 | 2 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async · W8 browser net |
| `net.pump` | C | 8 | 8 | 0 | 0 | yes | — | — | yes |  | W3 +RS · W5 +Py · W8 browser net |
| `net.ssh` | C | 16 | 16 | 1 | 11 | partial | yes | — | yes | poll exists; some paths block | W1 pure C · W2 +RS · W5 +Py · async · W8 browser net |
| `net.tftp` | C | 1 | 1 | 0 | 0 | no | — | — | yes |  | W3 +RS · W5 +Py · async · W8 browser net |
| `net.tls` | C | 18 | 18 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async · W8 browser net |
| `net.wg` | C | 12 | 12 | 0 | 3 | partial | yes | — | yes | RS FFI partial / uncounted | W3 +RS · W5 +Py · async · W8 browser net |
| `pack` | C | 6 | 6 | 0 | 0 | yes | — | yes | yes |  | W3 +RS · W5 +Py |
| `rt` | RS | 12 | 5 | 12 | 0 | yes | — | — | yes |  | W1 pure RS · W2 +C · W5 +Py · W7 browser |
| `shell.tui` | C | 4 | 4 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async |
| `shell.ui` | C | 2 | 2 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async |
| `shell.vt` | C | 8 | 8 | 0 | 0 | partial | — | — | yes |  | W3 +RS · W5 +Py · async |
| `trust` | C | 6 | 6 | 0 | 3 | yes | yes | yes | yes |  | W2 +RS · W5 +Py |
| `unix.x86` | Py | 2 | 0 | 0 | 2 | no | — | — | — | host sim sync | keep Py muscle · W4 +C/+RS bridges · W5 async |
| `unix.x86_64` | Py | 2 | 0 | 0 | 2 | no | — | — | — | host sim sync | keep Py muscle · W4 +C/+RS bridges · W5 async |
| `util.ascii` | C | 5 | 5 | 0 | 0 | yes | — | yes | yes |  | W2 +RS · W5 +Py · W7 browser |
| `util.eightcc` | C | 9 | 9 | 0 | 2 | yes | yes | yes | yes |  | W2 +RS · W5 +Py · W7 browser |
| `util.endian` | C | 7 | 7 | 0 | 7 | yes | yes | yes | yes | `*_inline`; +WIRE_IS_LE on Py | W2 +RS · W7 browser |
| `util.fourcc` | C | 9 | 9 | 0 | 2 | yes | yes | yes | yes |  | W2 +RS · W5 +Py · W7 browser |
| `util.lz4` | RS | 3 | 3 | 3 | 3 | yes | yes | yes | yes |  | W1 pure RS · W7 browser |
| `util.size` | RS | 2 | 2 | 2 | 2 | yes | yes | yes | yes |  | W1 pure RS · W7 browser |
| `util.tar` | RS | 5 | 5 | 5 | 2 | partial | yes | — | yes | sync walk today | W5 +Py · async · W7 browser |
| `wamr_host` | RS | 13 | 4 | 13 | 0 | partial | — | — | yes | LINK_WAMR=1; guest may block | W1 pure RS · W2 +C · W5 +Py · async |

---

## Snapshot (as of last edit)

| Metric | Value |
|--------|------:|
| Rows | 69 |
| Full export (C∧RS∧Py @ 100%) | 2 (`util.lz4`, `util.size`) |
| Async = yes | count the `yes` column when editing |
| Py = 0% | still the common gap for gfx / most net / fs |

Recompute the snapshot numbers when you bulk-edit the table.
