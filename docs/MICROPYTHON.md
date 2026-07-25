# Metal + MicroPython — design brief

Core scripting VM: **one Python blob**, **N equal runners**, same async
discipline as C Metal. Callable from **host and guest** — still one native
engine (not a wasm µPy, not a CPython appliance).

**Status:** spike **landed** on EFI + BIOS (linked, boot-proofed, shell +
guest surfaces exercised in `verify`). Cooperative run-lock, dotted-name
lookup, sync/async mismatch guards, exception/cancel/OOM isolation, a
generic `PM_METAL_PY_BIND` table, `pmcmd.*`/`pymergetic.metal.process`/
`pymergetic.metal.mod` bindings, guest-visible resolve+call of a bound
Python function (**sync and async**), isolated/`FRESH` mod instances from
Python (and dual-ABI guest-to-guest), generated `.pyi` stubs, and a signed +
trust-checked + single-flight-HTTP-fetched `stdlib.zip` are all now real and
boot-proofed (see [Implementation status](#implementation-status)). The
former **async-engine hang** is root-caused and fixed (see
[Resolved](#resolved--gc-stack-scan-boundary-captured-once-vs-resumed-cross-cpu)).
Real gap left: still a global run-lock instead of task-local GC (see
[Implementation status](#implementation-status)).  
**Product stays:** thin async host + awaitable ABI. µPy is a **face**, not a second kernel.

Related: [`COOP_MEMORY.md`](COOP_MEMORY.md) · [`IO.md`](IO.md) · [`LIBC_ASYNC.md`](LIBC_ASYNC.md) · [`TODO.md`](TODO.md)

---

## Decisions (locked)

| Topic | Lock |
|-------|------|
| Runtime | **MicroPython** (not CPython for core scripting) |
| Placement | **Native core** (firmware-linked), not a second wasm interpreter |
| Callers | **Host and guest** — same engine, two bindings (see below) |
| Scripts | Data on ESP / HTTP / embed — update without flashing the VM |
| Python ↔ Metal | **Both directions:** `metal.*` → C (bind table) and **C → Python** (call trampoline); same sync/async classes |
| OS orchestration | Guest dual-ABI surface mirrored as a **nice `metal.*` package** — optional anytime, never required for boot |
| Loadable natives | **Signed wasm/AOT**; self-register declares import name (no `metal.ext` ghetto); deny-list reserved — not `.so` / `dlopen` |
| Modules | **frozen** minimal core; **signed zip+HTTP** stdlib pack; see below |
| Import order | **builtin → frozen → aot → wasm → py** (same name); zip/ESP only supply aot/wasm/py — never beat builtin/frozen; **`metal` + verify stay frozen** |
| Trust | Same story as mods ([`TRUST.md`](TRUST.md)): verify `.sig` **before** zip on `sys.path`; **fail closed** on bad/missing when enforce |
| Dual “kernel + wasm” Python engines | **No** — one blob, many callers |
| Scheduler identity | **Python task = Metal task** — FCFS on N equal runners; no private Python loop, no CPU0 pin |
| Interpreter lifetime | **One always-on µPy blob** — shell/`py` does **not** start a new VM; it spawns a **new task** on that machinery |
| Ports | Same matrix as the rest of Metal — **spike EFI x64**, then BIOS/i386 like every other feature |
| Long-term shell | **Python REPL replaces the console as the system's main shell** (boot spawns an interactive REPL task instead of/ahead of the C UI console prompt) — see [Later — Python REPL as the system's main shell](#later--python-repl-as-the-systems-main-shell-replaces-the-console); not spike scope |

Image rollouts already refresh firmware; independent VM packages buy little.
Scripts remain the updatable unit.

---

## Call surface — host and guest

One µPy. Two ways in (same pattern as the rest of Metal: one ABI, host body vs
wasm import).

```text
Host (shell / C)  ──pm_metal_py_*──────────────┐
                                                │
                                                ▼
Guest (wasm)      ──import pymergetic.metal.py──►  µPy blob (MAP) + runners
                                                │
Python tasks      ──metal.* / await─────────────►  pm_metal_* (async/sync)
```

| Direction | Host | Guest (wasm) |
|-----------|------|----------------|
| Start / eval / create Python task | `pm_metal_py_*` C API (shell `py`, boot hooks) | Same shapes as **wasm imports** (`pymergetic.metal.py`) |
| Python → C | Bind table → `pm_metal_*` / loaded native | N/A inside VM — guest uses dual-ABI for those ops |
| C → Python | `pm_metal_py_call` / `py_call_async` trampoline | Same via `pymergetic.metal.py` call imports |
| Results / awaits | Metal handles both ways | Guest parks on handle like any other async op |

Guests do **not** embed their own µPy. They ask the core engine to run script /
spawn a Python task (bytes or path + args), optionally `await` a handle.
Host shell and C do the same without the import trampoline.

### Shell: `py`

The interpreter blob is **brought up with the OS** (or on first use) and **stays
up** — same MAP machinery, binds, zip mount, GC spaces.

```text
py myscript.py                     # script path
py -c 'print(1)'                   # snippet (shell quotes supported)
py -f c_py_demo.add 2 3            # C→Py sync call (2 ints → print i32)
py -f c_py_demo.blink 50000        # C→Py async call (1 int → shell job)
        │
        ▼
pm_metal_py_run_script / run_str / fn_bind+call*
        │
        ▼
create Metal/Python task on the existing blob  →  schedule on any runner
        │
        ▼
script runs (may await metal.*); shell may await the task handle or detach
```

| Rule | Lock |
|------|------|
| VM | **One** blob — never `py` ⇒ new interpreter process |
| Unit of work | **New Python task** (= Metal task) for that script / `-c` / async `-f` |
| Concurrent | Many scripts / REPL / C→Py calls can be in flight on the same blob |
| REPL | `py` with no script (or `py -i`) = interactive task on the same blob |
| Failures | One script’s exception/OOM does not tear down the interpreter |

Spike v1 can be host-only (`py`); guest import is the same ABI, second binding.

---

## Python ↔ C trampolines

**Need:** both directions without hand-rolled µPy glue per function, and a
**strict** map from Python sync/`await` onto Metal’s C sync/async classes
([`LIBC_ASYNC.md`](LIBC_ASYNC.md) / [`IO.md`](IO.md)).

```text
Python ──bind──► C (pm_metal_* / ext export)
C       ──call──► Python (callable / coro) ──await?──► Metal handle
```

Same idea as wasm `NativeSymbol[]` + `*_native_register()`, or shell
`PM_METAL_SHELL_CMD*`: **tables + class tags**, install at µPy init (and on ext load).

### Py → C (bind)

```text
pm_metal_py_bind(…)   →  installs into a module path (firmware metal.* or loaded native)
pm_metal_py_*         →  start/eval/task control / native load
```

Conceptual row (shape, not final typedef):

| Field | Role |
|-------|------|
| `mod` / `name` | Python path, e.g. `metal.aio` / `sleep_us` |
| `fn` | C `pm_metal_*` (or thin wrapper / ext trampoline) |
| `class` | **`sync`** \| **`async`** \| **`façade`** \| **`omit`** (omit = do not bind) |
| `sig` | Arg/result mapping (ints, buffers, handles — Metal types, not inventing a second ABI) |

Rules:

- **Classify first** in `LIBC_ASYNC.md` / `IO.md`, then bind. No drive-by Python-only APIs.
- One C body; Python is a face. Do not reimplement FS/net in the VM.
- Bind tables live next to the C module (like `*_native_register`); µPy init walks them once.
- Optional: linker-section collect (shell-cmd style) so adding a row is local to the feature.
- Firmware binds are the static half; **loadable wasm/AOT natives** append rows at runtime (below).

### C → Py (call trampoline)

**Need:** host C (shell, boot hooks, drivers that opt in, wasm host side) can invoke
Python callables — callbacks, policy hooks, scripted orchestration — with the
**same** sync/async discipline as Py → C. Not a blocking “run the interpreter until
the function returns” on a Metal runner when the Python side needs to `await`.

**Also:** C call sites should feel like **normal functions**, not a bag of
`py_call(ref, argc, …)` — via small **typed natural wrappers** over the trampoline.

```text
pm_metal_py_call(…)         →  sync: run callable to a value (bounded; no await inside)
pm_metal_py_call_async(…)   →  async: start callable/coro → Python task handle → await like any op
pm_metal_py_ref / lookup      →  resolve name or keep a handle to a Python callable
pm_metal_py_fn_* / macros     →  “natural” typed faces (below)
```

| C wants | API shape | Runner behavior |
|---------|-----------|-----------------|
| Sync result | `pm_metal_py_call` or typed `fn(…)` | Enter µPy briefly; callable must **not** `await` / park; bounded CPU |
| Async work | `pm_metal_py_call_async` / typed async fn → handle | Spawn/resume a Python task; C (or guest) **awaits** the handle; Python may `await metal.*` inside |
| Fire-and-forget | async call + no awaiter, or explicit detach | Still a Python task; errors go to log / task status — don’t block the caller |

#### Natural types (C feels like a normal func)

Low-level trampoline stays generic. Happy path = **typed fn objects** (or thin
macros) bound to a sig once, then called like C:

```c
/* declare once — sig is C types, class is sync|async */
PM_METAL_PY_FN(add,   int32_t, (int32_t a, int32_t b), SYNC);
PM_METAL_PY_FN(blink, void,    (uint32_t ms),          ASYNC);

/* bind to a Python callable (name or ref) */
pm_metal_py_fn_bind(&add, "c_py_demo.add");
pm_metal_py_fn_bind(&blink, "c_py_demo.blink");

/* sync — looks like a normal call */
int32_t sum = 0;
pm_metal_py_fn_call(add, &sum, 2, 3);   /* or: add.call(&sum, 2, 3) */

/* async — returns Metal handle; await as usual */
pm_metal_async_handle_t h = pm_metal_py_fn_call_async(blink, 10);
/* await h … */
```

Sketch of the carried type (not final):

```c
typedef struct pm_metal_py_fn {
	pm_metal_py_ref_t ref;
	uint8_t class;          /* sync / async */
	/* sig / marshalling cookie — fixed at PM_METAL_PY_FN declare time */
} pm_metal_py_fn_t;
```

| Rule | Lock |
|------|------|
| Feel | Call sites read like normal C functions / bound fn objects |
| Truth | Still the same trampoline + Metal handles underneath |
| Sig | Declared in C (`PM_METAL_PY_FN`); mismatch with Python is a runtime error, not silent UB |
| Async | Typed async fn **returns a handle** (or fills `*out_h`) — never blocks the runner |
| Guest | Same idea optional later as dual-ABI; host C gets the macros first |

Raw `pm_metal_py_call*` remains for dynamic/vararg cases; natural fns are the
default for known hooks.

```text
# C (async) — natural
h = pm_metal_py_fn_call_async(blink, 10);
await h;

# C (sync) — natural
pm_metal_py_fn_call(add, &sum, 2, 3);

# C (dynamic) — still available
rc = pm_metal_py_call(ref, …);        // fail if Python tries to await
```

Rules:

- **No fake sync:** if the Python callable may `await`, C must use **async** natural fn / `call_async` and await the handle.
- Sync `call` **rejects** (error) if the bytecode hits a Metal park — never spin the runner.
- Args/results use the same sig mapping as binds (Metal types ↔ Python objects in the task space).
- Callable refs are resolvable by dotted name (`mymod.hook`) or by an opaque `pm_metal_py_ref_t` kept after import/eval.
- Guests that need “ask Python” use `pymergetic.metal.py` call/async imports — same trampoline, dual-ABI face.
- Do **not** re-enter Python from a sync bind while already inside a sync `py_call` on the same task (no surprise recursion storms); document reentrancy: prefer async call from C for anything non-trivial.

#### Guest-visible call trampoline (dual-ABI handle trio)

A wasm guest never sees an `mp_obj_t` — it drives a resolved Python callable
the same "resolve once, call by handle" way Python drives a resolved mod
function via `pymergetic.metal.mod` (`guest/mod/mod_py_bind.c`):

```c
pm_metal_py_fn_h_t      pm_metal_py_fn_resolve(const char *dotted_name);
int32_t                 pm_metal_py_fn_call(pm_metal_py_fn_h_t h, uint32_t out_dest, int32_t a, int32_t b);
pm_metal_async_handle_t pm_metal_py_fn_call_async(pm_metal_py_fn_h_t h, uint32_t arg0);
```

`pm_metal_py_fn_h_t` is a fixed-slot handle table (`py.c`'s `mPyFnHandles`,
mirrors `mod.c`'s `ModFnHandleAlloc`/`Get`), imported from wasm as
`pymergetic.metal.py.pm_metal_py_fn_resolve` etc. (`py_guest.c`). Host callers
that already hold a `pm_metal_py_fn_t` from `pm_metal_py_fn_bind` use
`pm_metal_py_fn_call_async_bound` directly instead of burning a handle slot.
Sync call `out_dest` is a **guest linear-memory offset**, not a translated
pointer — WAMR's `*`/`~` marshaling only bounds-checks 1 byte without an
explicit length arg, so the native wrapper manually validates + writes
exactly `sizeof(int32_t)` (same pattern as `pm_metal_process_info`).

**Boot-proofed:** both resolve + sync call (`c_py_demo.add`) and resolve +
**async** call (`pm_metal_py_fn_call_async` against `c_py_demo.blink`),
folded into the existing `async_py` guest proof
(`mods/tests/async_py/main.c`) as a second nested task — the configuration
that used to hang before the [GC stack-scan boundary
fix](#resolved--gc-stack-scan-boundary-captured-once-vs-resumed-cross-cpu).

### Loadable extensions (wasm / AOT)

**Need:** add C-shaped callables to Python **without flashing** and **without** `dlopen`
([`LIBC_ASYNC.md`](LIBC_ASYNC.md): package load ≠ `dlopen`). The Metal plugin unit is
already **signed wasm / AOT** — reuse that for Python extensions.

```text
mods/py/native/<pkg>/   (.py tree + .aot|.wasm +.sig)   # mixed package OK
        │
        ▼  .py → import from folder; .aot/.wasm → load (trust)
   instantiate / self-register (declares fitting name, e.g. acme.codec)
        │
        ▼
   Python call → trampoline → native export (sync) or guest_step/await (async)
```

| Piece | Lock |
|-------|------|
| Package | Same as apps/tests: ESP + HTTP seed, Mods CA / `.sig`, prefer `.aot` then `.wasm` |
| Register | **Self-register** on load — well-known export (e.g. `pm_metal_py_native_register`) **or** bind imports; host installs rows |
| Unload | Drop bind rows tied to that instance; in-flight awaits cancel/fail; no dangling trampolines |
| Classes | Same **sync / async / façade** tags; async natives use Metal handles + park |
| Trust | Verify **before** register; fail closed under enforce — never bind from an unverified pack |
| Import name | **Self-register declares** dotted path (nanobind-style); disk path is load-only — **no forced `metal.ext` prefix** |
| Policy | **Deny-list** reserved names (`metal` core / verify, builtins, …). Additive names OK (`coolcodec`, `acme.x`). Reject shadowing frozen firmware modules. |
| Not | CPython `.so`, `ctypes`, `dlopen`, second µPy inside the native; folder path inventing the import name; a dumping-ground `metal.ext` package |

Firmware `pm_metal_*` binds stay in the image. Wasm/AOT natives are for **optional /
updatable** surfaces (codecs, app glue, demos) that still want a C ABI and Python
face. Pure-Python stays in the stdlib zip; heavy native work → loadable aot/wasm.

**vs guest py jobs:** a normal guest *starts* Python in the core VM.
A **native** *adds* callables *to* that VM. Same loader family, different role.

### Sync / async map (both directions)

| Class ([`LIBC_ASYNC.md`](LIBC_ASYNC.md)) | Py → C | C → Py |
|-----------------------------------------|--------|--------|
| **sync** / **façade** | Ordinary Python call → C returns | `pm_metal_py_call` → value (no await in Python) |
| **async** | Python gets awaitable → `await` | `pm_metal_py_call_async` → handle → C/guest `await`s |
| **omit** | Not on `metal.*` | Not callable from C |

```text
# Py → C (async)
h = metal.aio.sleep_us(1000)
await h

# Py → C (sync)
t = metal.aio.mono_us()

# C → Py (async, natural)
h = pm_metal_py_fn_call_async(blink, 10);  await h;

# C → Py (sync, natural)
pm_metal_py_fn_call(add, &sum, 2, 3);
```

**Hard rules:**

1. An **async** C API must **not** be bound as a blocking Python call (no “wait on the runner until done”).
2. A **sync** C API must **not** be wrapped in a fake `await` that parks for no reason.
3. C must **not** use sync `py_call` for Python that may `await` — use `call_async` + await the handle.
4. Python `await` **only** means “park this Python task on a Metal handle” — same family as wasm `guest_step` + `pm_metal_async_await`, stem = µPy task pump.
5. No `setjmp` across await; no holding exclusivity across await (same as C).
6. Buffers: host pointers / task-local bytes in the µPy blob — not guest linear offsets (that’s the wasm binding).

### Who calls what

| Caller | Uses |
|--------|------|
| Host C / shell | `pm_metal_py_*` run/eval; **`py_call` / `py_call_async`** into Python; binds already installed |
| Python | `metal.*` + any registered native packages → firmware C or loaded wasm/AOT |
| Wasm guest (app) | `pymergetic.metal.py` start/await **py job** or **call/call_async** — not a second bind table for every `pm_metal_*` |
| Wasm/AOT **native** | Load → self-register bind rows → Python calls trampoline into that instance |

Guests that need FS/net keep using `pymergetic.metal.fs` / etc. Python scripts that
orchestrate the OS use the mirrored `metal.*` package below (same C bodies).

### Python package shape (orchestrate the OS)

**Goal:** if someone wants, Metal is **orchestrateable from Python** — load mods,
await FS/net, kick UI/shell, sleep/yield — via a **clear module tree**, not a flat
bag of names. Optional anytime; boot/drivers must not depend on it.

Mirror the guest import modules (`pymergetic.metal.<area>`) as Python packages
under frozen/`metal` (bind table fills the leaves):

```text
metal/                    ← OS face only (firmware binds)
├── async/  fs/  net/  …
├── wasm/                 ← load / run guest jobs
└── py/                   ← eval / call / load native (path in, name from register)

# natives are normal top-level (or dotted) packages — not under metal.ext
coolcodec/                ← registered by coolcodec.aot
acme/
└── foo/
```

| Rule | Lock |
|------|------|
| Naming | `metal.<area>[.<sub>]` ↔ `pymergetic.metal.<area>[.<sub>]` — same split as headers |
| Semantics | Same `pm_metal_*`, same sync/async class — Python is a face, not a second ABI |
| Ergonomics | Prefer short Pythonic names at the leaf (`await metal.aio.sleep_us(…)`); keep `pm_metal_` out of the happy path |
| Coverage | Grow with the dual-ABI surface; new guest module ⇒ matching `metal.*` bind rows (or explicit omit) |
| Optional | No boot path imports `metal` for bring-up; shell/`py` and user scripts opt in |
| Host-only | APIs that are host-only in C stay host-only in Python (or omit) — don’t fake a guest import |

### Module structuring (nanobind-shaped)

Same split as nanobind / CPython extensions:

| Concern | Who decides | Analogy |
|---------|-------------|---------|
| **Where the binary lives** | Filesystem / package layout on ESP | `.so` under `site-packages/…` |
| **Where it lands in `import`** | **Declared inside the module** at register/init | `NB_MODULE(name, m)` / extension module name |
| **What attributes it has** | Bind/register table on that module | `m.def("sleep_us", …)` |

**Filesystem path ≠ import path** (unless we choose a default). Loading finds the
artifact; **self-register says the Python name**.

#### Pure Python (frozen / zip / ESP scripts)

Normal Python: **folder tree is the import path** (`metal/net/http/__init__.py`
→ `metal.net.http`). No separate “declare name” step.

#### Firmware C leaves on those packages

Packages exist as the freeze/zip tree. C only **attaches functions** onto an
already-named module (like `m.def`):

```text
pm_metal_py_bind("metal.aio", "sleep_us", …)   # module path must already exist
```

#### Loadable wasm / AOT natives (the nanobind case)

No `metal.ext` dumping ground. **Mixed trees are fine:** `.py` packages and
`.aot`/`.wasm` sit in the same folder layout. Pure Python uses folder→import;
wasm/AOT **self-registers** to the fitting dotted name (should match where it
belongs in that tree).

```text
mods/py/native/acme/                 # on sys.path (or loaded as a unit)
  __init__.py                        → import acme              (folder = name)
  util.py                            → import acme.util
  _codec.aot  + _codec.aot.sig       → load → register("acme.codec", …)
  _codec.wasm                        # fallback if no aot
```

```text
# .py — no register step
import acme
import acme.util

# native — path load, name from inside (authoritative)
metal.py.load("mods/py/native/acme/_codec.aot")
import acme.codec                    # only if register said "acme.codec"
```

| Rule | Lock |
|------|------|
| **Mixed tree** | `.py` + `.aot`/`.wasm` (+ `.sig`) in one package dir — normal |
| **`.py` import name** | Folder / file layout (Python-normal) |
| **wasm/AOT import name** | Declared in **self-register** (like `NB_MODULE`); register should fit the tree (`acme.codec` next to `acme/`) |
| **Same-name order** | Global: **builtin → frozen → aot → wasm → py** (see Import order). Natives/py never override builtin/frozen. |
| **Disk path** | Find/load/verify for the binary; not a second naming scheme |
| **Convention** | Stage under `mods/py/native/<pkg>/`; register matches the intended submodule — **register still wins** |
| **Policy** | **Deny-list** reserved names; frozen/`metal` already win via order |
| **Load trigger** | Explicit `metal.py.load(path)` and/or import-hook when a native sibling is needed — spike picks one; both OK later |

```text
import acme.codec
  1. builtin (if any)
  2. frozen
  3. acme/_codec.aot  (+ .sig) → verify → register → use
  4. else acme/_codec.wasm (+ .sig) → …
  5. else acme/codec.py → …
  6. fail
```


**Spike:** mixed `sample/` with one `.py` + one `.aot` that registers as
`sample.native` — prove folder py + register wasm. Firmware `metal/async/`
stays folder=import.

Orchestration examples (product intent, not spike scope):

```text
await metal.fs.open_async(...)
await metal.net.http.get(...)
metal.wasm.run("mods/apps/…")     # or await job handle
await metal.aio.sleep_us(…)
```

### Spike slice for this machinery

- One **sync** bind (`mono_us` or similar).
- One **async** bind (`sleep_us`) + `await` from Python.
- One **C → Py** natural fn (`PM_METAL_PY_FN` + sync/async call) over the trampoline.
- Prove a third C function is “add a table row” — not new VM glue.
- Package root **`metal.aio` only** for the sample — shaped like the tree; not a flat dump and not full `fs`/`net` yet.
- **Sample zip** (tiny + `.sig`) so the loader integrates; fat pack later.
- Later: one tiny signed native that self-registers as e.g. `sample` (not under `metal.ext`).

---

## Modules — freeze vs zip vs HTTP

Three tiers (like libc async classes). Changing a row = rebuild policy, not a
drive-by `require()`.

| Tier | Meaning |
|------|---------|
| **frozen** | Baked into firmware (`manifest.py`). Fallback + non-overrideable core. |
| **zip** | On ESP as a package (zipimport / `.mpy` tree). Update without flash. |
| **http** | Same bytes as zip; seed from `:8080` / next-server when missing (+ `.sig` per trust mode). |
| **omit** | Not in Metal v1 — no fake CPython stdlib. |

**Rule:** freeze only what first `py` needs with empty ESP and no net, plus the
trust/bridge surface that must not be replaceable from the pack. Everything else
is **one stdlib zip** (ESP cache + HTTP fill). User scripts are separate
files/zips, not the stdlib package.

### frozen (core)

| Module / surface | Notes |
|------------------|--------|
| `metal` | Frozen package tree → guest dual-ABI / `pm_metal_*` — **never shadowed by zip**; orchestration face |
| Verify / pack mount | Path that checks `.sig` and mounts the zip — stays in firmware / frozen |
| Builtins / core VM | `micropython`, `gc`, `sys`, `errno`, `struct`, `array` (C builtins; not on `sys.path`) |
| Tiny types | `collections` (minimal), `memoryview` / `array` as shipped by port |
| Async kernel | Metal-shaped awaitables (not full CPython `asyncio` / stock `uasyncio` until wired) |
| `json` | Config / small RPC — cheap and always useful |
| `binascii`, `hashlib` (if small) | Trust / hex / digests for scripts touching `.sig` paths |

Keep this list **short**. Prefer C extmod already in the µPy port over freezing
large pure-Python. Anything frozen **wins** over aot/wasm/py of the same name —
freeze only what must not be field-updated.

### zip (+ http seed) — stdlib / libs package

Served/staged like mods, e.g. `mods/py/stdlib.zip` (+ `.sig`).

**Spike sample:** ship a **tiny** zip (one/two modules) so loader + trust +
import order integrate; grow contents later. Do not block the spike on a fat
micropython-lib pack.

| Module / surface | Notes |
|------------------|--------|
| Sample stub(s) | Minimal non-frozen module(s) for import proof |
| `uasyncio` / richer async helpers | If not in frozen Metal await layer |
| `ure` / `re` | Regex — optional for many scripts |
| `io`, `os` / `uos` façades | Thin; real FS is `metal` async |
| `time` façades | Clocks sync; sleep → Metal async only |
| `socket` façade | Must not block — wrap Metal net or omit until ready |
| `ssl` / `requests`-like | Prefer Metal `http`/`net`; zip only if thin helpers |
| `logging`, `argparse`, `pathlib`-lite | Nice for apps; not boot-critical |
| `unittest` / test helpers | Dev image / HTTP only |
| Third-party micropython-lib | Explicit allowlist in package manifest |

HTTP = same zip when ESP miss (blank metal). No second “online-only” module set
unless a row says **http-only** below.

### Trust (same as mods)

Stdlib zip is a **Mods-CA** artifact — same PKI / modes as wasm packs
([`TRUST.md`](TRUST.md)): `off` / `soft` / `enforce`.

| Rule | Lock |
|------|------|
| When | Verify **before** the zip is put on `sys.path` |
| Bad signature | **Fail closed** — do not mount; import fails |
| Missing `.sig` | Per trust mode (enforce requires it; soft/off as mods) |
| Keys | Same Mods CA pubs / `./scripts/pki` story as `mods/apps/*` |

Example artifact: `mods/py/stdlib.zip` + `stdlib.zip.sig`.

### Lazy fetch (single-flight)

On first miss (no usable ESP zip):

1. One Metal task **fetches** stdlib zip (+ `.sig`) over HTTP (timeout + retry).
2. Concurrent importers **async-wait** on that same handle — not N parallel downloads.
3. Verify → cache to ESP → mount on `sys.path` → waiters resume import.
4. Other Metal work keeps running while waiters are parked (same as any await).

Failure after retries → normal `ImportError` (no half-mounted zip).

### http-only (optional later)

| Module / surface | Notes |
|------------------|--------|
| Large demos / examples | Never freeze; never required on ESP for boot |
| Board-specific experiment packs | ThinkPad / QEMU extras |

### omit (v1)

| Module / surface | Why |
|------------------|-----|
| `threading` / `_thread` | Metal tasks/coros only |
| `multiprocessing`, `subprocess` | No |
| Full CPython `asyncio` | Wrong shape; Metal await + thin helpers |
| `socket` blocking stdlib as product | Same as retiring WASI blocking I/O |
| `ctypes`, `mmap`, `signal`, `select` | Preemptive-OS surface |
| pip / arbitrary PyPI | Explicit packages only (zip/http allowlist) |
| `dlopen` / CPython `.so` / `ctypes` | Exts are **wasm/AOT** + bind register; see above |

### Import order (product)

For a given dotted name, always:

```text
1. builtin     C builtins (sys, gc, …) — not a path lookup
2. frozen      firmware freeze / immortal
3. aot         verified .aot native → self-register
4. wasm        verified .wasm native → self-register
5. py          .py / .mpy on ESP, zip, or script paths
6. fail
```

Signed zip / `mods/py/native/` / HTTP seed only **provide** candidates for steps
3–5. They do **not** outrank builtin or frozen. Mount zip only after trust check.

**`metal` + verify** stay frozen (or builtin-side) so packs cannot replace them.
To make a module updatable without flash, **don’t freeze it** — ship aot/wasm/py
instead.

---

## Memory

Wasm/WAMR today lives on **HEAP** (TLSF): pool, `os_malloc`, and WAMR’s
`os_mmap` → `BH_MALLOC`. MAP is stacks + virtio/DMA (+ future fixed blobs).

µPy:

| Piece | Where |
|-------|--------|
| Interpreter + frozen modules | Firmware (`.text` / `.rodata`) |
| One MAP **blob** | GC / task spaces (long-lived carve) |
| Scripts | FS / HTTP / embed (not the VM) |

```text
MAP:   stacks | virtio/DMA | µPy blob | …
HOLE:  …
HEAP:  TLSF | WAMR pool | coros | …
```

`mem`'s `map` row now breaks the µPy blob out as its own nested line (size +
% of `map`) instead of leaving it invisible inside the aggregate — see the
[Implementation status](#implementation-status) checklist.

---

## Concurrency — same schematic as Metal async

**Goal:** one memory tree, different points of async execution, **1 runner per
core** — Python tasks pumped like C tasks. No Python-visible GIL.

```text
MAP µPy blob (one tree)
├─ immortal / frozen / code     ← shared, parallel read OK
├─ task-local GC spaces         ← each running task mutates only its space
└─ cross-task share             ← Metal only (handles, messages, ids)
```

| Metal C | µPy-shaped |
|---------|------------|
| Task / coro state | **Python task = Metal task** + task-local GC space |
| Params / published ids | Args + Metal handles (not shared Python object graphs) |
| Stackless `await` | Python `await` → park; resume on any runner |
| `create_task` / `await task` | Same Metal ops; thin `metal.aio` mirrors ([`COOP_MEMORY.md`](COOP_MEMORY.md)) |
| Runner stack (MAP) | Short native slice while in bytecode |

### Rules

1. **Parallel bytecode** across runners is required — same blob, many tasks in flight (plus other Metal work on those CPUs).
2. **No GIL API.** Exclusivity is lock-by-invisibility: switch only at `await`; never hold a lock across `await` (same as C).
3. **Mutable Python state is task-local.** Cross-task sharing = Metal, not shared object graphs. Passing a live Python object to another task is **out** (error / forbidden) — share handles, bytes copies, or published ids.
4. **GC dissolved into async:**
   - Task at `await` → may GC its nursery (invisible).
   - Blob-wide compact → Metal barrier: every Python task parked, then GC, then resume.
5. **Non-starvation:** a tight sync Python loop holds its runner. Authors must `await metal.aio.yield_()` (or equivalent) for fairness; optional later: bytecode timeslice that injects a yield. Starving FS/net/wasm on that CPU is a bug in the script or missing yield, not a second scheduler.
6. **Cancel / death:** Metal task cancel stops the Python task; uncaught exception or OOM kills **that** task only — not the blob, not other runners. Sync `py_call` that hits a park → hard error (never spin).

Flat shared mutable heap with N cores allocating freely is **out** (concurrent-GC research). Partitioned task spaces inside one blob **are** the Metal-shaped answer.

### Task-local GC (integration contract)

| Topic | Lock |
|-------|------|
| Layout | One MAP blob; per-task nursery/space + immortal/frozen shared read-only |
| Alloc | Running task allocates only in its space |
| Cross-task | No borrowed Python refs across tasks; Metal handles / copies only |
| Nursery GC | At park (`await`) or explicit yield — invisible to authors |
| Compact | All Python tasks parked → barrier → compact → resume |
| Caps | Blob size + max Python tasks + per-task nursery limits (constants; tune in spike) |

Spike may start with **one** task space and prove await/runners first; multi-space + barrier is required before calling concurrency “done.”
Until task-local GC (§5), bytecode entry uses a **global py run-lock** (released at Metal `await` park) so overlapped sleeps are safe on equal runners.

---

## Async integration

Python `await` bottoms out in Metal handles via the **bind table** (above).
Resume is the host trampoline / runner inbox — same family as `guest_step` for
wasm, but the stem is the µPy **task step** (Metal task), not a wasm export and
not a private asyncio loop.

| Do | Don’t |
|----|--------|
| Park at Metal await; resume on any runner | Bind async C as blocking Python |
| Task-local heaps + Metal share | Pretend stock µPy is multi-core on one flat heap |
| Sync CPU work between awaits | `setjmp` / NLR unwind across await |
| Classify then bind (`LIBC_ASYNC`) | Invent Python-only I/O beside `pm_metal_*` |
| `await` only Metal awaitables / py-task handles | Stock `uasyncio` as the kernel event loop |

### Awaitable type

One product awaitable: wraps `pm_metal_async_handle_t` (ops) or a Python-task
handle (`call_async` / nested py job). µPy’s `await` machinery targets that —
Metal is the scheduler; helpers in zip may look like asyncio but must not own CPUs.

### µPy port guts (required for integration)

| Piece | Lock |
|-------|------|
| CONFIG | Trimmed port; no threads; no blocking sleep as product |
| NLR / exceptions | Must compose with park/resume; **no** `setjmp` across await |
| `.mpy` ABI | Pinned to the firmware µPy version (frozen + zip + scripts lockstep) |
| Reentrancy | Prefer async C→Py for non-trivial work; sync bind → sync `py_call` nesting on the same task is restricted (document hard errors) |

---

## Integration checklist (async-first)

What “fully integrated” means beyond packaging. Product tree / zip / exts are
breadth; this table is the **runtime core**.

### Must have (runtime)

| Item | Notes |
|------|--------|
| Python task = Metal task | FCFS, any runner; no private loop |
| Metal awaitables | `await` → handle park/resume |
| Task-local GC + compact barrier | See contract above |
| Non-starvation | `yield` (required path); timeslice optional later |
| Cancel / isolation | One task dies; blob lives; sync call can’t fake-wait |
| Port guts | NLR, `.mpy` pin, trimmed CONFIG |

### Product edges (easy to forget)

| Item | Notes |
|------|--------|
| I/O face | `print` → shell/UART; Ctrl-C / cancel; tracebacks |
| Sched mirrors | `metal.aio` create_task / await task / yield / sleep |
| Caps | Blob / task / nursery limits visible (and in `mem`) |
| Host tests | Link µPy + await proofs without full EFI where practical |
| Hot replace | Zip remount / ext unload while tasks live — reject or barrier+drain |

### Sample (enough to integrate the zip loader)

Integration does **not** wait on full `metal.fs` / `net` / … coverage. Need a
**thin sample** so the signed-zip path is real end-to-end:

| Piece | Sample (spike) |
|-------|----------------|
| Binds | `metal.aio` only — e.g. `mono_us` (sync) + `sleep_us` / `yield_` (async) |
| Stdlib zip | Tiny pack (one or two non-frozen modules, e.g. a stub `logging` or `ure`) + `.sig` |
| Loader | ESP hit → mount (trust); miss → single-flight HTTP → verify → cache → mount; import order still builtin→frozen→aot→wasm→py |
| Import proof | sample name from zip (py or native); `import metal` still frozen / unshadowable |
| Scripts | One ESP `.py` that awaits `sleep_us` and imports the sample zip module |

That is enough to lock trust, `sys.path` order, and single-flight. Rich zip and
full orchestration tree come after.

### Breadth (after sample + core)

Full `metal.fs` / `net` / …, fat stdlib zip, loadable exts, BIOS/i386 — same
bring-up pattern as the rest of Metal; not blockers for “async integrated.”

---

## Spike order

1. ~~Link trimmed µPy into **EFI x64**; MAP-carve always-on GC blob; shell `py` <script> → new task (not new VM); `print` → shell.~~ **done**
2. ~~Python task = Metal task; one sync + one async bind (`sleep_us`); **C → Py** `call` / `call_async` (`py -f`).~~ **done**
3. ~~Two Python tasks overlapping awaits; equal runners (no CPU0 pin); `yield` fairness path.~~ **done** (boot proofs; run-lock, not true parallel bytecode — see task-local GC)
4. ~~Guest binding: wasm import that starts a py job + `await` completion (proof mod).~~ **done** (`mods/tests/async_py`)
5. Task-local spaces (or staged path to them) + cancel/isolation; note blob size vs Doom HEAP. Cancel/isolation now **done** (`PY_PROOF_EXC`/`CANCEL`/`OOM`); task-local GC spaces still **open**.
6. ~~**Sample zip loader:** tiny signed `stdlib.zip` + frozen `metal.aio`; ESP/HTTP single-flight; import proof + one script.~~ **done** — `stdlib.zip` + `.sig`, trust-checked, single-flight HTTP fetch on ESP miss, import-unshadowable regression proof (`PY_PROOF_SHADOW`)
7. ~~Same bring-up on **BIOS / i386** as any other Metal feature (not a separate product decision).~~ **done** — `py/py.c` links into both `scripts/build.d/port/efi/default.sh` and `.../port/bios/default.sh`; `stdlib.zip` stages into both ESP and PXE trees
8. ~~Generic bind table (linker-section rows); `pymergetic.metal.*` naming consistently everywhere (guest imports and Python module tree); `pmcmd.*` short-name exception for shell commands; `pymergetic.metal.process`/`pymergetic.metal.mod` bindings; guest-visible resolve+call of a bound Python function (sync **and async**).~~ **done**

---

## Later — C++ (not spike)

µPy’s extension surface is a **C API**. C++ can ride that without a second VM:

| Stage | What |
|-------|------|
| Later | `.hpp` façades / re-exports over `pm_metal_*` and the py bind/call trampolines (same sync/async classes) |
| Later still | Optional C++ ↔ Python bridge (nanobind-shaped helpers) that **emits or wraps** the same bind table — not a parallel ABI |

No C++ in the µPy spike. Don’t invent a second Python↔native calling convention for C++.

---

## Later — Python REPL as the system's main shell (replaces the console)

Long-term direction (not spike scope, tracked for after the rest of the
spike lands): the interactive surface the user actually types into stops
being `shell.c`'s own C line-editor dispatching into
`pm_metal_shell_cmd_dispatch`'s C command table, and becomes a **Python
REPL task** running on the one always-on µPy blob — same async multicore
engine, no new VM, no new scheduler, no second interactive loop.

| Stage | What |
|-------|------|
| Boot | Instead of (or ahead of) offering today's UI console prompt, boot spawns a Python REPL task directly — the interactive task the [REPL row](#call-surface--host-and-guest) above already designs for (`py` with no script / `py -i`), just made the thing boot actually starts rather than an on-demand shell subcommand |
| Welcome | REPL startup prints a short banner that introduces the engine — what it is (async, multicore, one shared interpreter across N runners), and that `pymergetic.metal.*` / `pmcmd.*` are already live — not a bare `>>>`. Needs an actual name/identity for "the async multicore Python engine" this banner speaks as; bikeshed the name later, don't block on it now |
| Compat | `pmcmd.*` (every C shell command already a Python callable, see [Surfaces](#implementation-status)) is the exact bridge this needs — today's line-oriented `help`/`mem`/`keyb`/... commands stay reachable as `pmcmd.help()` etc. from the REPL, nothing lost by switching the primary surface |
| C console | `shell.c`'s own line editor is not deleted as part of this step — stays available as a fallback/serial-style path (or gets retired in a later, separate decision). REPL becomes **primary**, console stays reachable |

**Real prerequisite, not started:** `py -i` (an actual interactive REPL
task — persistent globals dict across lines, prompt + continuation-line
handling, one `mp_lexer_new_from_str_len`/compile/exec per submitted line
instead of per whole script) doesn't exist yet; the REPL row in the
Concurrency table is design intent today, not implemented code. Build that
first as an ordinary boot-proofed interactive task on the existing blob,
*then* wire boot to spawn it as the default surface — don't try to skip
straight to "boot replaces the console" without the REPL task itself
existing and being proven first.

---

## Out of scope / do not

- Host **CPython** as core scripting
- A **second** µPy/CPython inside wasm guests (guest calls core; it does not ship a VM)
- Python-visible GIL / threads / `stop-the-world` APIs for authors
- Claiming N-way parallel mutation of one flat µPy heap without task-local spaces
- Making drivers or boot depend on Python (orchestration is **optional**)
- Native `dlopen` / CPython extension `.so` as the loadable-ext story
- A flat, unnamespaced dump of every `pm_metal_*` into Python builtins
- Stock **uasyncio / CPython asyncio** as the kernel scheduler (Metal runners own CPUs)
- C++ as a requirement for the Python spike (hpp / cpp-py bridge are **later** only)

---

## Resolved — GC stack-scan boundary captured once vs. resumed cross-CPU

Was tracked as "known issue — second nested async task hangs
OOM-isolation" while boot-proofing the guest-visible **async** call path
(`pm_metal_py_fn_call_async`); root-caused and fixed (chat transcript
`f5dd0bb0-957d-4a63-84f4-0052970d4d99` has the full bisection trace that
led here). Left in place as a **root-cause writeup**, not a live issue.

**Symptom:** the boot sequence tolerated exactly **one** coroutine-backed
Python async task spawn total across the whole run; a second one anywhere
(even a bare repeated `pm_metal_py_run_script`, no new code) hung the boot
sequence completely — no crash, no traceback, no log output at all, not
even the OOM proof's own 3 s deadline check — right before `PY_PROOF_OOM`.
Bisection ruled out the handle table, `py_run_lock`, mod count, which mod,
and the target script/function; it was specifically "the boot sequence as a
whole spawns a second coroutine-backed async task, anywhere, ever."

**Root cause:** `mp_embed_init()` (`py.c`'s `pm_metal_py_init`) calls
`mp_stack_set_top()` **exactly once**, at boot, from whichever CPU happens
to run boot init. That writes one global field,
`MP_STATE_THREAD(stack_top)` — confirmed single-global via
`MICROPY_PY_THREAD == 0` in
[`py/embed/mpconfigport.h`](../src/pymergetic/metal/py/embed/mpconfigport.h)
(no per-thread state array). But Metal's scheduler deliberately does **not**
pin a guest/task to one CPU (`runtime/async/async.c`'s own comments:
"Guest tasks no longer share a CPU... two call-ins can now genuinely race
across runners") — `py_run_lock` only serializes *when* Python bytecode
runs (one CPU at a time), never *which* CPU. So a Python coroutine step
(`py_job_step`) resumed via the ready-ring's work-stealing can land on a
different CPU's native stack than the one recorded at boot. Every
`gc_collect()` then computes its scan range in
[`shared/runtime/gchelper_generic.c`](../external/micropython/shared/runtime/gchelper_generic.c)'s
`gc_collect_root` as an **unsigned, unchecked** subtraction:
`((uintptr_t)MP_STATE_THREAD(stack_top) - (uintptr_t)&regs) / sizeof(uintptr_t)`.
Once bytecode resumes on the "wrong side" of the one recorded `stack_top`,
that subtraction wraps to a huge word count, and `gc_collect_root` walks it
with no yield point and no bounds check — a silent, unbreakable spin. This
matches every symptom, and explains why it needed **two-plus** tasks (higher
chance work-stealing resumes on a non-boot CPU at least once) and broke
specifically at the **OOM proof** (the one script that reliably forces a
real `gc_collect()` by exhausting the whole 256 KiB blob; lighter scripts
may never trigger a real collection cycle at all).

**Fix:** `py_run_lock` already guarantees at most one CPU executes Python
bytecode at any instant, so it's always safe to re-anchor the scan boundary
to *this* call, on *whichever* CPU currently holds the lock, right before
running bytecode. Added `#include "py/stackctrl.h"` to `py.c` and a
`mp_stack_set_top(&nlr)` call at the top of every real bytecode-entry point:
`py_exec_and_maybe_main`, `py_call_bound`, `py_resume_coro`, and
`pm_metal_py_call`. No pinning, no locking changes — a ~4-line diff, safe
because the boundary only needs to be valid for the duration of the one
synchronous call it guards (GC only ever runs synchronously inside these,
never after they return).

**Verified:** restored the `async_py` guest proof plus a second, independent
nested-task spawn (`mods/tests/async_py/main.c` now also exercises
`pm_metal_py_fn_call_async` against `c_py_demo.blink`); `./scripts/verify
efi`/`bios` under `-smp 4` — `PY_PROOF_EXC`/`CANCEL`/`OOM` and every proof
after them now print `ok` with no timeout, on both ports.

**NLR / park-resume safety:** unaffected by this bug and unrelated to the
fix above — `nlr_push`/`nlr_pop` are correctly scoped inside each
non-parking call (`py_exec_and_maybe_main`, `py_call_bound`,
`py_resume_coro`), so a Python exception's non-local return never crosses
an `await` park/resume boundary; the `nlr_buf_t` lives on the C stack of one
synchronous call and is always popped before that call returns control to
the scheduler. Nothing park-resumes *through* an active `nlr_push`.

**Blob vs. Doom HEAP:** the µPy blob is a MAP-side carve
(`PM_METAL_PY_BLOB_BYTES = 256 KiB`, `py.h`); Doom's own allocations go
through the HEAP-side TLSF pool via wasm/WAMR. Per the dual-span arena
([`COOP_MEMORY.md`](COOP_MEMORY.md)), MAP grows up from the low side and
HEAP grows down from the high side of the **same** claimed hole — they
don't share one allocator, but they do compete for the same underlying
memory: a larger µPy blob (or heavier concurrent Doom heap use) shrinks the
same hole both sides are drawing from, so the practical ceiling when both
run concurrently is `hole_size`, not each side's own nominal budget.

---

## Implementation status

Validated against the tree on 2026-07-25 (code, not just intent). Checkboxes
below replace the earlier aspirational pass — see inline notes for what was
overclaimed.

### Runtime integration

- [x] µPy linked in core, on **both EFI and BIOS** (`scripts/build.d/port/{efi,bios}/default.sh` both compile `py/py.c`); MAP-carved blob (`pm_metal_py_init` → `pm_metal_mem_map(PM_METAL_PY_BLOB_BYTES)`)
- [x] MAP blob visible in `mem` **with its own line/caps** — `pm_metal_py_blob_bytes()` getter (`py.h`/`py.c`) + a nested `py` line under `mem`'s `map` row (`shell_core_cmds.c`'s `CoreMemCmd`) showing the blob's carved size and its % of `map`, instead of only being counted in the aggregate
- [x] Python task = Metal task; pumped on N equal runners (`pm_metal_async_create_task` over the normal coro/task machinery — no private loop)
- [x] `await` Metal op from Python without blocking the runner (`py.c`'s `py_job_step`/`py_resume_coro` park via `pm_metal_async_await`, releasing the run-lock first)
- [x] ≥2 Python tasks with overlapped awaits on multi-CPU — proven at boot (`boot_python.c`'s `PY_PROOF_OVERLAP`: two scripts sleeping 150 ms each finish in <250 ms wall); **run-lock interim** (see task-local GC below)
- [x] No Python GIL surface added; cross-task sharing goes through Metal handles only — true by absence (nothing new was added), not independently tested/enforced
- [x] Cancel / exception / OOM isolates one task; sync `py_call` cannot park — `pm_metal_py_call` and `py_call_bound` (`py.c`) both explicitly check `mp_obj_is_type(ret, &mp_type_gen_instance)` and hard-error on a sync/async mismatch instead of relying on incidental `.send()` `AttributeError`s; boot-proofed (`boot_python.c`'s `PY_PROOF_EXC`/`PY_PROOF_CANCEL`/`PY_PROOF_OOM` — uncaught exception, task cancel mid-sleep, and blob-exhausting `MemoryError` each isolate to that one task, blob stays live for the next proof)
- [x] Fairness path: `metal.aio.yield_` proven under load — boot proof `PY_PROOF_YIELD` (`yield_peer.py` vs `yield_sleeper.py`)
- [x] `.mpy`/µPy version pinned — `scripts/setup.d/deps/micropython.sh` pins `v1.24.1` (single vendored checkout, patches applied on top)
- [x] Written note that NLR/exceptions are safe across park/resume — see the ["NLR / park-resume safety" note](#resolved--gc-stack-scan-boundary-captured-once-vs-resumed-cross-cpu) below; `nlr_push`/`nlr_pop` are correctly scoped inside each non-parking call and never cross an `await` boundary
- [x] Spike size/RAM note vs Doom — see the ["Blob vs. Doom HEAP" note](#resolved--gc-stack-scan-boundary-captured-once-vs-resumed-cross-cpu) below: MAP (blob) and HEAP (Doom's wasm allocations) grow from opposite ends of the same dual-span hole, so the practical ceiling when both run concurrently is the hole size, not each side's nominal budget

### Surfaces

- [x] Always-on blob; shell `py <script>` / `py -c` → new task on it; `print` → shell (`py_shell.c`, `mphalport_metal.c`)
- [x] Guest can start a py job via import and `await` completion — real, exercised end-to-end: `mods/tests/async_py/main.c` imports `pymergetic.metal.py`, awaits `pm_metal_py_run_script`, and is wired into the boot self-test suite (`boot_test.c`'s `async_py` case)
- [x] **Generic** bind table (rows → install at init, "add a function = add a table row, no new VM glue") — `PM_METAL_PY_BIND` macro (`py.h`) collects rows into a linker section (`.pm_metal_py_binds.*`, same pattern as `PM_METAL_SHELL_CMD`); `pm_metal_py_bind_table()`/`pm_metal_py_binds_install()` (`py_bind.c`) walk it at init and resolve/create the dotted parent module chain automatically. `metal.aio`'s `mono_us`/`sleep_us`/`yield_` (`runtime/async/async_py_bind.c`) are now table rows, not hand-written `mp_obj_dict_store` calls; adding a 4th binding is a one-line macro invocation. `docs/MODS.md`'s checklist item 9 ("µPy binds the same registries") is done.
- [x] C → Py trampoline: sync `pm_metal_py_call` + async `pm_metal_py_fn_call_async_bound` (await handle) — used by `py -f mod.fn` in the shell and the `c_py_demo` module seeded at init; plus a guest-visible dual-ABI handle trio (`pm_metal_py_fn_resolve`/`_call`/`_call_async`, see [Guest-visible call trampoline](#guest-visible-call-trampoline-dual-abi-handle-trio)) — **both sync and async boot-proofed** (`mods/tests/async_py/main.c`), the async path now unblocked by the [GC stack-scan boundary fix](#resolved--gc-stack-scan-boundary-captured-once-vs-resumed-cross-cpu)
- [x] Isolated/`FRESH` mod instances from Python — a mod-proxy's `.fresh` attribute (`mod_py_bind.c`) returns a callable that opens a fresh scope object; `async with mod.<name>.fresh() as inst:` works via MicroPython's generic attribute dispatch resolving `__aenter__`/`__aexit__` (no separate context-manager protocol needed), then per-function bound calls scoped to that handle (`await inst.fn(...)`); refuses (raises) on a `SINGLE`-capability mod. The underlying `pm_metal_mod_fresh_open`/`_resolve`/`_close` trio (`mod.c`, extracted out of `pm_metal_mod_fn_process`) is declared **dual-ABI** in `mod.h`, closing the asymmetry where `FRESH` used to be host-only — a wasm guest can now open a `FRESH` instance of another mod too. Boot-proofed from Python (two concurrent scopes don't share state, `mods/tests/fresh_counter/main.c`) and from a wasm guest-to-guest call (`mods/tests/fresh_guest/main.c`)
- [x] Public host-only value facade `py_obj.h`/`py_obj.c` (`pm_metal_py_obj_t` + int/str/dict/tuple/error helpers, plus the awaitable bridge `pm_metal_py_new_awaitable`/`_new_awaitable_u32` in `py_await.c`) — lets C code outside `py/` build/read Python values without including MicroPython's own headers. Each `pymergetic.metal.*` bind file now lives next to the subsystem it wraps instead of inside `py/`: `runtime/async/async_py_bind.c` (`aio`), `guest/process/process_py_bind.c` (`process`, fully facade-only — no MicroPython headers at all), `guest/mod/mod_py_bind.c` (`mod` — keeps real MicroPython headers for its lazy-attr custom types, the one part a facade can't cheaply abstract), `shell/shell/shell_py_bind.c` (`pmcmd`). `py/` itself keeps only core VM machinery (`py.c`, `py_bind.c`, `py_obj.c`, `py_await.c`, `py_shell.c`, `py_zip.c`, `py_guest.c`)
- [x] `metal.*` package mirrors guest areas — `pymergetic.metal.aio` (`mono_us`/`sleep_us`/`yield_`), `pymergetic.metal.process` (`poll`/`active`/`current`/`info`), `pymergetic.metal.mod` (attribute-based, lazy-resolved access to any loaded mod's registered functions — `mod.<name>.<func>(...)`, `AttributeError` on a bad name, awaitable result via `pm_metal_py_new_awaitable_u32`), and the short `pmcmd.<name>(*args)` exception (shell commands as Python callables) — all boot-proofed (`PY_PROOF_PMCMD`, `PY_PROOF_MOD`). Naming is consistently `pymergetic.metal.*` everywhere except `pmcmd.*` (the one deliberate short-name exception); orchestration optional (nothing on the boot path imports it)
- [x] Multi-level dotted name resolution (`pymergetic.metal.aio.sleep_us`, 3+ segments) — `pm_metal_py_lookup` (`py.c`) walks each dot-separated segment with its own `mp_load_attr`, fixing a bug where only the first segment split correctly; boot-proofed (`PY_PROOF_DOTTED`)
- [x] `.pyi` type stubs for editor/linter support — **generated**, not hand-written: `scripts/gen_py_stubs.py` parses `PM_METAL_PY_BIND` call sites (→ `typings/pymergetic/metal/**.pyi`), `PM_METAL_SHELL_CMD`/`PM_METAL_SHELL_CMDS` call sites (→ `typings/pmcmd.pyi`, docstring = the row's `help_str`), and `pm_metal_mod_register_func` call sites under `mods/**` (→ one stub class per in-tree mod, best-effort — mods loaded at runtime and absent from `mods/**` at build time get no stub); wired into both `scripts/build.d/port/{efi,bios}/default.sh` so stubs regenerate on every build
- [x] **Signed stdlib zip**: `mods/py/stdlib.zip` + `.sig` (signed via `scripts/pki sign-wasm`, which turned out generic enough for an arbitrary blob — no `sign-file` subcommand needed), verified via `pm_metal_trust_accept_mods` against `METAL_TRUST_MODE`; `pm_metal_py_zip_ensure()` is now a coroutine-backed step (`pm_metal_py_zip_step`, its own `PY_STEP_ZIP` ahead of every job's real first step in `py.c`) so it can `await` an HTTP fetch on ESP miss instead of blocking `py_spawn`; single-flight fetch (one leader task awaits `pm_metal_net_http_get` directly, follower tasks poll the shared state via `pm_metal_async_yield()` — `pm_metal_await`'s single-waiter limitation rules out every task awaiting the same handle directly). `pymergetic`/`pymergetic.metal`/`pymergetic.metal.aio` are pre-registered into `mp_loaded_modules_dict` at init, so they're structurally unshadowable by any same-named `.py` — regression-proofed with a decoy `pmcmd.py` written to `/mods/py/` at runtime (`PY_PROOF_SHADOW`, `boot_python.c`) asserting the C module's own attribute wins. Enforced `builtin→frozen→aot→wasm→py` import order across *all* categories is not separately implemented (MicroPython's own `mp_loaded_modules_dict` precedence already covers the one case that matters — C modules over zip `.py` files)
- [ ] (later) Task-local GC spaces + all-parked compact barrier — still the documented interim: one global `py_run_lock`/`py_run_unlock` spinlock in `py.c` serializes all Python bytecode execution across tasks (released only across `await`)
- [ ] (later) Fat stdlib zip / full `metal.fs`/`net`/… / native self-register (`import sample`) — not started
