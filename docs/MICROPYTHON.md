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
Python (and dual-ABI guest-to-guest), generated `.pyi` stubs, a signed +
trust-checked + single-flight-HTTP-fetched `stdlib.zip` with a real ~25-module
"Easy" pack, a persistent interactive **REPL** that's now the system's
default boot shell, and genuine **task-local GC + true parallel bytecode**
via opt-in isolated MicroPython contexts (own heap, own state, no shared
lock) are all now real and boot-proofed (see [Implementation
status](#implementation-status)). The former **async-engine hang** is
root-caused and fixed (see
[Resolved](#resolved--gc-stack-scan-boundary-captured-once-vs-resumed-cross-cpu)).
The shared/default context (REPL, `py -c`, `py <script>` without `-x`) still
serializes through one run-lock by design — that path's semantics
(persistent shared globals) are exactly what makes it suitable as the
system shell; isolated contexts are the opt-in escape hatch for genuine
parallelism.  
**Product stays:** thin async host + awaitable ABI. µPy is a **face**, not a second kernel.

Related: [`COOP_MEMORY.md`](COOP_MEMORY.md) · [`IO.md`](IO.md) (including
the ASGI `httpd` and Microdot surface) · [`LIBC_ASYNC.md`](LIBC_ASYNC.md) ·
[`TODO.md`](TODO.md)

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
| Async kernel | Metal-shaped awaitables (`await` → Metal handles); zip `asyncio` is a name shim only |
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
| `asyncio` (Metal shim) | `stdlib_src/asyncio/` — `sleep`/`Event`/`gather`/`create_task` over `pymergetic.metal.aio`; not a second event loop |
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

### micropython-lib categorization (Easy / Needs-glue / Defer)

`external/micropython/lib/micropython-lib` is already vendored (submodule
init in `micropython.sh:26`). Categorized by dependency shape, so pulling a
module into `mods/py/stdlib_src/` (see next section) is a lookup, not a
per-module design discussion:

**Easy — pure Python, zero OS/hardware/C-ext dependency, packed (verified by
the `PY_PROOF_STDLIB` boot proof, `boot_python.c`):**
`collections` (+ `collections-defaultdict`), `heapq`, `bisect`, `functools`,
`itertools`, `contextlib` (+ `ucontextlib`), `copy`, `struct`, `string`,
`pprint`, `operator`, `types`, `warnings`, `errno`, `keyword`, `abc`,
`quopri`, `html`, `argparse`, `stat`, `pickle`, `inspect`, `traceback`,
`logging`, `base64`, `fnmatch`. `binascii` rides along as a **C extmod**
(`extmod/modbinascii.c` — not part of upstream's embed package, so Metal's
own build scripts + `py/embed/micropython_embed.mk` compile and qstr-scan it
explicitly; see the [C-extmod-outside-the-embed-package
note](#c-extmod-outside-the-embed-package-modbinascii) below), not a packed
`.py` file — it needed a real build-system fix, not just an "Easy" copy.
`random`, `hashlib`, `re` are likewise real C extmods
(`extmod/modrandom.c`/`modhashlib.c`/`modre.c` + `lib/re1.5`), and `os`/`io`
are Metal's own pure-Python modules written directly against
`pymergetic.metal.fs` (not packaged upstream `.py` at all) — see [the four
extmods + os/io section](#random-hashlib-osio-re--three-c-extmods-one-own-pair)
below for all five.

Second pass over this list turned out less trivial than the doc's own
original guesses in a few cases — two more grammar/builtin flags joined
`SLICE`/`SET`/`ENUMERATE` in `py/embed/mpconfigport.h`, both self-contained
(no new heap shape, no cross-cutting behavior change) the same way those
three were:

- `MICROPY_PY_SYS_EXC_INFO` — `sys.exc_info()`, needed by
  `traceback.print_exc()`/`format_exc()` (and `logging`'s `exception()`).
  Zero-cost to enable: the backing `MP_STATE_VM(cur_exception)` root
  pointer (`py/vm.c`) is already set on every caught exception
  unconditionally, this flag only compiles in the Python-level accessor.
- `MICROPY_PY_BUILTINS_STR_OP_MODULO` — `"%s" % x`, needed by `logging`'s
  message formatting. Also quietly fixes a **latent, previously-unexercised
  gap** in `argparse`'s own error-message paths — `PY_PROOF_STDLIB`'s
  happy-path parse never took that branch, so the gap shipped silently in
  the first "Easy" pass.

Three more small **in-place deltas**, all because the module's own upstream
source assumed a stdlib neighbor this minimal build doesn't have — same
spirit as the `defaultdict`/`string.translate()` deltas below, not scope
creep:

- `logging.py` drops `import time` entirely (no wall-clock/RTC source —
  `LogRecord` never records a timestamp, `Formatter.formatTime()` always
  returns `None`, no `%(asctime)s` support), drops `import io` (its
  `exception()` prints straight through `sys.print_exception(tb)` instead
  of buffering into `io.StringIO` first), and defaults `StreamHandler`'s
  stream to `None` → `print()` instead of `sys.stderr` (`MICROPY_PY_SYS_STDFILES`
  is off) — `FileHandler` is dropped too since it needs both.
- `traceback.py`'s `print_exception()` no longer defaults a missing `file`
  to `sys.stdout` (`MICROPY_PY_SYS_STDFILES` off) — `sys.print_exception`
  (`py/modsys.c`) already defaults its own missing 2nd arg to the
  platform's plat_print stream, so the 1-arg call form is used instead.
- `pickle.py`'s `dumps()`/`loads()` work on `str` directly instead of
  round-tripping through `bytes` via `.encode()`/`.decode()`
  (`MICROPY_CPYTHON_COMPAT` off) — this "pickle" was already just
  `repr()`+`eval()`, never the real binary protocol, so the bytes step was
  pure overhead being removed, not a capability loss.

`stat` and `inspect` needed no changes at all — `stat` is pure bit-twiddling
constants/functions with zero imports, `inspect`'s only import (`sys`) was
already on.

Two small deltas from the original plan, both **fixes to this run's own
categorization**, not scope creep:

- `defaultdict.__new__`'s `super(defaultdict, cls).__new__(cls)` isn't
  reachable in this minimal build (`'super' object has no attribute
  '__new__'`, then `object.__new__` isn't reflectable either) — Metal's copy
  drops the override; nothing in the Easy pack subclasses `defaultdict`
  without calling `__init__`, so the upstream edge case doesn't apply here.
- `string.translate()` upstream builds its result with `io.StringIO` — `io`
  is Needs-glue, not packed. Metal's copy accumulates into a list and joins
  instead (same result, one fewer stdlib dependency); this is what
  unblocks `html.escape()`.
- `argparse` needs `enumerate()` — off by default under
  `MICROPY_CONFIG_ROM_LEVEL_MINIMUM` (gated at `AT_LEAST_CORE_FEATURES`) —
  so `MICROPY_PY_BUILTINS_ENUMERATE (1)` joins the `SLICE`/`SET` overrides
  already in `py/embed/mpconfigport.h`.

**Promoted out of Needs-glue once `re`/`os`/`hashlib` landed** (see the
[extmods + os/io section](#random-hashlib-osio-re--three-c-extmods-one-own-pair)
below): `os`, `os-path`, `io`, `re`, `random`, `hashlib`, `base64`,
`fnmatch` — all now in the Easy list above, all `PY_PROOF_STDLIB`/
`PY_PROOF_RANDOM`/`PY_PROOF_HASHLIB`/`PY_PROOF_OSIO`/`PY_PROOF_RE`
boot-proofed, zero source changes needed for `base64`/`fnmatch` themselves
beyond one more builtin flag (`MICROPY_PY_BUILTINS_BYTEARRAY`, for
`base64.py`'s own `_translate()`/decode path).

**Still Needs-glue after this pass** — each tried and rejected for a
concrete, specific reason (not just "not attempted"):

- `textwrap` — imported, but `TextWrapper`'s class body fails at
  `re.compile(...)` with `ValueError: error in regex`; its own pattern
  needs a regex feature re1.5 (this build's whole `re` engine, see
  [C extmod outside the embed package](#c-extmod-outside-the-embed-package-modbinascii))
  doesn't support (likely inline flags/lookahead) — not fixable by a config
  flag, would need a source-level pattern rewrite.
- `hmac` — imports `hashlib` fine, but `HMAC.name`'s `@property` needs
  `MICROPY_PY_BUILTINS_PROPERTY` (also gated at
  `MICROPY_CONFIG_ROM_LEVEL_MINIMUM`), not yet flipped — untried past the
  first failure, plausibly Easy too on a follow-up pass.
- `uu` — `binascii.b2a_uu`/`a2b_uu` don't exist in MicroPython's
  `extmod/modbinascii.c` at all (checked, not assumed); this is a hard
  upstream gap, not a Metal config flag.

**Promoted out of Needs-glue in the follow-up pass** — `time`, `datetime`,
`hmac`, `zlib`, `gzip`, `pathlib`, `shutil`, `tempfile`, `tarfile`,
`unittest`, `textwrap`, `uu` all now ship. `ssl` shipped too, but as
`pymergetic.metal.tls` — a Metal-flavored async TCP+TLS client, not a CPython
`ssl`-module-shaped shim (see below); `threading` is the one deliberate
permanent skip. See [Second Needs-glue pass — real
RTC/decompressor/tar/tzinfo/regex work](#second-needs-glue-pass) for what
each one actually needed and where the real gotchas were (three of them
weren't in the original guesses at all: no `f"{x:02d}"` support at
`MICROPY_CONFIG_ROM_LEVEL_MINIMUM`, small-int-only `timedelta.min`/`.max`
overflowing a tagged 62-bit int, and `str.expandtabs()`/`.translate()` simply
not existing in this MicroPython's `py/objstr.c` at all — an upstream gap,
not a config flag).

**`threading` — deliberately not shimmed, permanent, not "later":** Python
`threading`/`_thread` model real OS threads with a GIL serializing bytecode
between them. Metal has neither: there is no GIL (an isolated
`pm_metal_py_ctx_t` already *is* Metal's answer to "run Python bytecode in
parallel", see [Task-local GC spaces](#task-local-gc-spaces--real-parallel-bytecode)
above), and a Metal task is a stackless coroutine, not a preemptible OS
thread — `threading.Lock().acquire()` blocking would have to either busy-spin
the one CPU running that coroutine (livelock, since nothing else gets a turn
to release it) or secretly become an `await` point (silently changing
`threading`'s synchronous contract into something that isn't
`threading.Lock` anymore). Either shim lies about what the code is actually
doing. `pymergetic.metal.aio`/isolated contexts/`Task-local GC` are the real,
honestly-named primitives for "run Python concurrently on Metal" — `import
threading` staying an `ImportError` is the correct, permanent outcome, not a
gap to close later.

| Module(s) | Glue needed |
|-----------|-------------|
| `threading`, `_thread` | Not shimmed, ever — see above |

**Defer / not applicable yet — hardware, network, or ecosystem-specific,
revisit when those subsystems exist:** everything under `micropython/*`
(`bluetooth`, `drivers`, `usb`, `umqtt.*`, `net`, `espflash`, `lora`,
`senml`, `aiorepl`, `mip`, ...) and `python-ecosys/*` (`aiohttp`,
`requests`, `cbor2`, `pyjwt`, `iperf3`), plus `venv`, `pkgutil`/
`pkg_resources`, `locale`, `curses.ascii`.

### Second Needs-glue pass

Closed every remaining Needs-glue module from the table above except
`threading` (never — see above), one binding decision at a time:

- **`time`/`datetime`** — new `pymergetic.metal.time` (`dev/random/time_py_bind.c`)
  exposes `realtime_ms()`/`mono_us()`/`tz_minutes()`/`sleep_ms()`, backed by
  the *same* wall clock `random.c` already fed to wasm guests (EFI's
  `gRT->GetTime()`/BIOS's CMOS RTC, refined by SNTP — `dev/net/ntp.c`); there
  was already a real RTC source, it just hadn't been wired to Python yet.
  `mods/py/stdlib_src/time.py` is a from-scratch, int-primary implementation
  (float seconds work once `MICROPY_PY_BUILTINS_FLOAT` is on; wall path
  still prefers ms/us ints from `pymergetic.metal.time`) providing
  `time()`/`gmtime()`/`localtime()`/`mktime()`/
  `monotonic()`/`sleep()`. `datetime.py` is pulled fresh from
  micropython-lib on top of it, with two real, non-obvious deltas:
  1. it uses `f"{h:02d}"`-style f-strings throughout — `MICROPY_PY_FSTRINGS`
     defaults off at `MICROPY_CONFIG_ROM_LEVEL_MINIMUM` (`py/mpconfig.h`
     gates it at `AT_LEAST_EXTRA_FEATURES`), so every such module failed to
     even *parse* (`SyntaxError`, not a runtime gap) until the flag joined
     `SLICE`/`SET`/`ENUMERATE`/`PROPERTY` in `mpconfigport.h`.
  2. CPython's real `timedelta.min`/`.max` use `days=±999999999`, which needs
     a bignum (now `MICROPY_LONGINT_IMPL_MPZ`; the clamp below was from the
     earlier `NONE` era and can be revisited); worse, MicroPython's "small int" is a
     *tagged* machine word (one bit reserved for the object-tag), so it's a
     62-bit magnitude, not the 63/64 a raw `int64` would suggest — half of
     what the first, wrong fix assumed. Metal's copy clamps both sentinels
     to `±53375994` days (~146236 years), the largest magnitude that still
     fits `_us` with room for the trailing h/m/s/µs fields; a correctness
     non-issue for a sentinel value, verified against the actual `2**62`
     boundary, not guessed.
  `PY_PROOF_TIME` (`boot_python.c`) round-trips `mktime(localtime(now)) ==
  now` (**not** `gmtime` — `mktime` is documented as `localtime`'s inverse,
  never `gmtime`'s; the first version of this proof asserted the wrong pair
  and failed by exactly `tz_minutes()`'s offset every time), checks the Unix
  epoch's known weekday, and HMAC-SHA256s a known test vector through
  `hmac`+`hashlib`+`time`+`datetime` together.
- **`hmac`** — `MICROPY_PY_BUILTINS_PROPERTY` flipped on (`HMAC.name`/
  `.digest_size`); upstream `hmac.py` pulled in unmodified on top of
  `hashlib`+`time`.
- **`zlib`/`gzip`** — `extmod/moddeflate.c` wired into both ports' build
  scripts + `py/embed/micropython_embed.mk`'s qstr scan, `MICROPY_PY_DEFLATE`
  + `_COMPRESS` flipped on. `DeflateIO` needs a stream object with a real
  C-level stream protocol (`mp_stream_p_t`) — no pure-Python class can
  satisfy that — so `MICROPY_PY_IO`/`MICROPY_PY_IO_BYTESIO` were flipped on
  for the *native* `uio.BytesIO`, re-exported through Metal's own `io.py` as
  `from uio import BytesIO, StringIO` (the forced-alias path every
  extensible built-in gets for free — `py/objmodule.c`). `gzip.py` patched
  `builtins.open` → `io.open` (no `builtins` module here); both modules'
  `_WBITS`/`_MAX_WBITS = const(15)` became plain `= 15` (`MICROPY_COMP_CONST`
  off, no runtime `micropython` module either).
- **`pathlib`/`shutil`/`tempfile`** — `pathlib.py` patched `from micropython
  import const` away (same `const()` gap) and redirected `open()` through
  `io.open`; needed `os.getcwd()` (Metal is single-rooted, always returns
  `"/"`) and `os.ilistdir()` (built from `listdir()`+`stat()` per entry) added
  to `os/__init__.py`. `shutil.py` upstream has no `copyfile()` at all — added
  a minimal one (`io.open` + `copyfileobj`). `tempfile.py` upstream only had
  `mkdtemp()`/`TemporaryDirectory` — added `NamedTemporaryFile`/
  `TemporaryFile` (auto-delete-on-close wrapper). `io.py`'s `FileIO.__init__`/
  `open()` accept-and-ignore an `encoding=` kwarg for `pathlib`'s
  `read_text()`/`write_text()`.
- **`tarfile`** — bound Metal's *existing* C microtar (`util/tar.c`, already
  used for mod packaging) directly as a `pymergetic.metal.tar` facade
  (`util/tar_py_bind.c`, a slot table over `pm_metal_util_tar_iter_t`/
  `_writer_t`), then `mods/py/stdlib_src/tarfile.py` wraps that in a small
  CPython-`tarfile`-shaped `TarInfo`/`TarFile`/`TarWriter` surface — not a
  port of upstream `tarfile.py` (which assumes a full `os`/file-descriptor
  layer this build doesn't have), a from-scratch facade over an engine Metal
  already had.
- **`unittest`** — needed `io.StringIO` (added to `io.py`'s `uio`
  re-export, same one `zlib`/`gzip` needed) for traceback capture, plus a
  proof that actually instantiates `unittest.TestSuite()` and
  `suite.addTest(T)` rather than guessing at the constructor shape.
- **`textwrap`** — the real blocker was never a config flag: `re.compile()`
  on `wordsep_re` raises `ValueError: error in regex` because `lib/re1.5`
  (this build's whole `re` engine) has no lookahead/lookbehind support at
  all. `TextWrapper._split()` was rewritten as a manual character scanner
  (`_split_words_metal`) replicating the same word/hyphen-boundary rules
  without regex; `dedent()` similarly rewritten regex-free
  (`text.split("\n")` + a `_leading_ws_metal` helper, since `re.MULTILINE`
  isn't honored either). Two more upstream calls turned out to not exist in
  this MicroPython **at all**, an upstream gap rather than a config gate:
  `str.expandtabs()` and `str.translate()` aren't implemented anywhere in
  `py/objstr.c`, full stop. `_munge_whitespace()` now calls two small
  from-scratch replacements (`_expandtabs_metal`: column-tracking tab
  expansion; `_translate_ws_metal`: linear char-remap) instead.
- **`uu`** — `binascii.b2a_uu`/`a2b_uu` don't exist in MicroPython's
  `extmod/modbinascii.c`, confirmed hard gap, not a flag. `uu.py` implements
  `_b2a_uu`/`_a2b_uu` from scratch and `encode()`/`decode()` against
  file-like objects (`io.BytesIO`); `decode()` requires an explicit
  `out_file` rather than CPython's guess-a-filename-from-the-header
  behavior, since there's no filesystem-path convention to guess into here.
- **`ssl` → `pymergetic.metal.tls`, not a CPython `ssl` shim** — Metal has no
  socket object with a `.makefile()`/blocking-read shape for a CPython-style
  `ssl.wrap_socket()` to sit on top of; instead this is a new, honestly
  Metal-flavored async TCP+TLS client. `dev/net/tls_conn.c` is a slot table
  (`tls_conn_slot_t`, one socket + mbedTLS context + wire buffers per slot)
  driving one coroutine step function (`TlsConnOpStep`) through DNS resolve
  → connect → TLS handshake → read/write, all through the same
  `pm_metal_async_await` model every other Metal I/O primitive uses.
  `dev/net/tls_py_bind.c` exposes `pymergetic.metal.tls.{open,connect,write,
  read,close}` to Python; `read()` needed a new awaitable shape
  (`pm_metal_py_new_awaitable_bytes`/`pm_metal_py_await_bytes_fn` in
  `py_obj.h`/`py_await.c`) since the existing awaitable bridge only ever
  returned ints, not a `bytes` payload sized by however many bytes the TLS
  layer actually had ready.

### Network + FS/IO polish (`pymergetic.metal.net`, `io.IOBase`, `json`)

Not a Needs-glue-tier module — a separate, explicitly-scoped follow-up: a
thin async socket facade plus two cheap fs/stdlib polish items.

- **`pymergetic.metal.net`** (`dev/net/net_py_bind.c`) — a thin wrapper over
  `dev/net/net.h` (the same lwIP-backed primitive every other net path in
  this codebase already uses), same honest-primitive spirit as
  `pymergetic.metal.tls`: no CPython `socket`/`select`-shaped shim, no
  blocking calls. `socket([domain[, type]])`/`bind_if`/`send`/`close` are
  plain sync calls; `connect`/`listen`/`accept`/`recv`/`dns` are
  await-bridges over `net.h`'s own async handles — no local state machine
  needed here (unlike `tls_conn.c`, which needs one because a TLS handshake
  chains multiple `net.h` ops together; `net.h`'s own primitives are each
  already a single async op). `recv()` needed a small per-socket scratch
  buffer table (`NetPyRecvSlotFor`, 8 slots × 4 KiB) since
  `pm_metal_net_recv(h, ptr, len)` writes into `ptr` for the *entire*
  operation's lifetime, not just at completion — a fresh
  `pm_metal_mem_alloc`-per-call buffer freed inside the awaitable's resolve
  callback would be a use-after-free (the actual bytes copy happens right
  after that callback returns, still reading the pointer it set). `socket()`
  takes optional `domain`/`type` args defaulting to `AF_INET`/`SOCK_STREAM`
  rather than exposing named integer constants — `PM_METAL_PY_BIND` binds
  callables, not plain values, so sensible defaults were the simpler
  ergonomic answer.
- **`pymergetic.metal.net.http`** (`dev/net/net_http_py_bind.c`) — a nested
  submodule (dotted-path resolution handles the nesting for free, no extra
  plumbing) exposing `get(url)` (await → `bytes` body, one static
  lazily-allocated 64 KiB scratch buffer — a REPL-driven one-GET-at-a-time
  facade, not a connection pool) and `last_status()` (sync, mirrors
  `net.dns_last_ntoa`'s "await the op, read the separate last-result call"
  idiom).
- **`PY_PROOF_NET`** (`boot_python.c`) — offline-safe: loopback-only
  listen/accept/connect/send/recv plus `dns('localhost')`, no real network
  required (deliberately no online proof, same policy as every other net
  path in this codebase). First exercise of `listen()`+`accept()` in the
  Python binding layer; needed an explicit `pm_metal_net_poll()` call inside
  the proof's own wait loop — `pm_metal_async_session_pump()` (the shell's
  main loop) already calls it, but the boot-proof driver only called
  `pm_metal_run_poll_all()`, so lwIP's stack never progressed and the proof
  hung until this was added.
- **`io.IOBase` for real on-disk streams** — `MICROPY_PY_IO_IOBASE` flipped
  on; `io.py`'s `FileIO` now subclasses the native `uio.IOBase` and
  implements `readinto()`/`ioctl()`, wiring it into the real C-level stream
  protocol slot (`mp_stream_p_t`) the same way `uio.BytesIO` already
  carries it. This is what lets `gzip.open()`/`deflate.DeflateIO` wrap an
  actual on-disk file, not just a `BytesIO` — checkpointed in
  `PY_PROOF_ARCHIVE` (`gzip.open()` write-then-read round-trip against a
  real `/mods/py/...` file, not just the pre-existing `compress`/
  `decompress` BytesIO round-trip). One real gotcha:
  `MICROPY_PY_ARRAY_SLICE_ASSIGN` (bytearray `buf[:n] = data`) is gated off
  at this build's `MICROPY_CONFIG_ROM_LEVEL_MINIMUM` — `FileIO.readinto()`
  copies byte-by-byte instead (single-index item assignment has no such
  gate), and `shutil.copyfileobj()`'s `readinto()` fast path (now taken,
  since `FileIO` didn't have `readinto` before this) had to drop a
  `memoryview(buf)[:sz]` call for the same reason — a plain `buf[:sz]`
  slice *read* is unaffected, only slice *assignment* is gated.
- **`json`** — `MICROPY_PY_JSON` (+`_SEPARATORS`) flipped on;
  `extmod/modjson.c` wired into both ports' build scripts and
  `py/embed/micropython_embed.mk`'s qstr scan (same "extmod C file outside
  the embed package" pattern as `modbinascii.c`/`modrandom.c`/…, see the
  section above — the qstr scan needs `SRC_QSTR +=` *before*
  `py/mkrules.mk` is pulled in, a later `+=` is too late for its
  `qstr.i.last` rule). No floats involved at compile time
  (`modjson.c` has no unconditional float reference), so this doesn't
  reopen the `MICROPY_PY_BUILTINS_FLOAT` decision. `dumps()`/`loads()`
  round-trip checkpointed inside `PY_PROOF_FSMOD`.
- **Explicitly out of scope** (unchanged from the original net plan): no
  CPython-shaped `socket`/`select` module, no `ssl.wrap_socket()`-on-a-
  socket-object (that's what `pymergetic.metal.tls` is for), no
  `umqtt`/`requests`/`aiohttp` ports, no new VFS/mount table, no
  per-context CWD, no FS jail.

### Reproducible `stdlib.zip` build — baked in, not tracked

`mods/py/stdlib_src/` stages the **Easy** list's `.py` files (flat copy from
micropython-lib, one directory per module/package, small in-place fixes
where the minimal build genuinely can't support the upstream line — see the
deltas noted above; no other build-time transformation) — that's the real,
git-tracked source of truth. `mods/py/build_stdlib_zip.sh` zips that tree
deterministically (sorted filenames, fixed mtimes, `ZIP_STORED` via
Python's own `zipfile` module — no external `zip` binary dependency, no
host-path leakage) into `mods/py/stdlib.zip`, then `scripts/pki sign-wasm`
produces `stdlib.zip.sig`.

`mods/py/stdlib.zip` and `stdlib.zip.sig` themselves are **not** tracked in
git (`.gitignore`) — the `.sig` in particular is never byte-stable across
re-signs (ECDSA signing is randomized), so committing it would mean
constant, meaningless diffs on every rebuild. Instead,
`scripts/build.d/port/efi/embed-stdlib.sh` — run unconditionally by both
`scripts/build.d/port/{efi,bios}/default.sh`, right alongside
`embed-mods.sh`'s guest-wasm embed — calls `build_stdlib_zip.sh` and then
embeds the freshly-built zip and signature bytes as two `static const
uint8_t[]` arrays in a generated `src/pymergetic/metal/py/py_zip_embed.inc.c`
(also gitignored, regenerated every build). `py_zip_embed.c` writes those
bytes out to the real `PM_METAL_PY_STDLIB_ZIP` / `_SIG` filesystem paths
once, at `pm_metal_py_init()` time, before anything else touches them — so
`py_zip.c`'s trust-check/import machinery (`ZipVerifyLocal`, below) sees an
ordinary signed file on "disk" and needs zero special-casing for the
embedded case. Net effect: a fresh clone + fresh build always has a
locally-signed `stdlib.zip` ready with no network fetch and nothing
signature-shaped sitting in git history. Lazy HTTP fetch-on-miss
(below) remains as a fallback for the (today hypothetical) case where the
embedded write itself fails.

`PY_PROOF_STDLIB` (`boot_python.c`) imports **every** packed module (not a
sample) and exercises one real call per module — `defaultdict` increments,
`heapq` push/pop, `bisect`, `functools.reduce`, `itertools.chain`,
`copy.copy`, a `contextlib.contextmanager`, `errno`/`keyword`/`abc`
lookups, a full `argparse` parse, `struct.pack`/`unpack`, `binascii.hexlify`,
and `html.escape` — so a module that imports but is subtly broken (wrong
builtin alias, missing extmod flag) fails loudly instead of just "not
raising `ImportError`". Any module that turned out incompatible with
`MICROPY_CONFIG_ROM_LEVEL_MINIMUM` during this pass either got a small,
noted in-place fix (see above) or was demoted to Needs-glue (`hmac`, `uu`,
`textwrap` — see [Still Needs-glue after this
pass](#micropython-lib-categorization-easy--needs-glue--defer) above)
rather than silently breaking the zip. `base64`/`fnmatch` were demoted here
too on the first pass, then promoted back once `re` landed — proved
separately by `PY_PROOF_RE`, not this step, since they were added in the
later `re` pass, not the original Easy pass.

#### C extmod outside the embed package: `modbinascii.c`

`extmod/modbinascii.c` isn't part of upstream's `ports/embed/embed.mk`
package at all — that makefile only copies `extmod/modplatform.h` into the
generated tree (confirmed by reading it: `py/*.[ch]` gets copied wholesale,
`extmod/` gets exactly one header). Everything Metal freezes today that
*looks* like a C extmod (`modcollections.c`, `modheapq.c`, `modstruct.c`,
`moduerrno.c`) actually lives under upstream's `py/` directory, which is why
they "just worked" once the matching `MICROPY_PY_*` flag was set — they were
already inside the embed package's scan scope. `binascii` is the one Easy
module backed by a real `extmod/*.c` file, so it needed two additional,
Metal-owned build fixes beyond flipping its config flag:

1. **Compile it at all:** `external/micropython/extmod/modbinascii.c` is
   added directly to both port build scripts
   (`scripts/build.d/port/{efi,bios}/default.sh`) alongside the `py_*.c`
   glue files, instead of coming from the generated
   `build/micropython_embed/` tree.
2. **Register it:** compiling isn't enough — `MP_REGISTER_EXTENSIBLE_MODULE`
   only lands in the generated `moduledefs.h` (and `MP_QSTR_binascii` only
   lands in `qstrdefs.generated.h`) if the file is part of the **qstr scan**
   (`SRC_QSTR`), which for the embed port is normally just `py/*.c`. Metal's
   own `py/embed/micropython_embed.mk` adds
   `SRC_QSTR += $(MICROPYTHON_TOP)/extmod/modbinascii.c` **before**
   `include $(MICROPYTHON_TOP)/ports/embed/embed.mk` — the qstr/moduledefs
   rule's prerequisite list is expanded once, at the point `py/mkrules.mk`
   is parsed (inside that `include`), so appending to `SRC_QSTR` afterwards
   is silently too late (the rule already has its own frozen prerequisite
   list; verified the hard way — a same-run `+=` placed after the `include`
   produced a clean build with `binascii` compiled in but still
   `ImportError: no module named 'binascii'`, because the module simply
   never got scanned for either kind of definition).

Any future Easy module that's genuinely backed by an `extmod/*.c` file
(rather than a `py/mod*.c` one) needs the same two-part treatment — check
which directory backs it before assuming the C-ext flag alone is enough.

#### `random`, `hashlib`, `os`/`io`, `re` — three C extmods, one own pair

The four biggest items on the old Needs-glue list, closed in one pass:

- **`random`** — `extmod/modrandom.c`, a complete C extmod (same
  two-part treatment as `binascii` above: added to both port build
  scripts' Sources **and** `micropython_embed.mk`'s `SRC_QSTR`).
  `MICROPY_PY_RANDOM`/`MICROPY_PY_RANDOM_EXTRA_FUNCS` on;
  `random()`/`uniform()` are available once floats are on
  (`MICROPY_PY_BUILTINS_FLOAT=1`). Seeded once at boot from a
  real `pm_metal_random()` draw — `dev/random/random_py_bind.c`'s
  `pymergetic.metal.random.seed_u32()`, the one hand-written Python bind
  this module needed — instead of the extmod's fixed compile-time
  default seed, so successive boots don't replay the same sequence.
- **`hashlib`** — `extmod/modhashlib.c` with SHA-256 via the
  self-contained `lib/crypto-algorithms/sha256.c` fallback (no
  mbedtls/axtls hookup in this build, so MD5/SHA1 stay off).
  `#include "lib/crypto-algorithms/sha256.h"` needed `external/micropython`
  itself (the bare root, not just `extmod/`) added to both ports'
  include paths — EDK2's `build` tool auto-derives `-I` for each
  *Sources* file's own directory, but doesn't walk `#include`s to find
  transitive ones, so this one path had to be added by hand to
  `Metal.inf`'s `[BuildOptions]` (a fully static, hand-maintained
  `CC_FLAGS` line — confirmed by reading `scripts/build.d/port/efi/
  default.sh`'s own Metal.inf-touching code, which only ever splices the
  `BEGIN_MICROPYTHON`/`END_MICROPYTHON` Sources block, never
  `[BuildOptions]`) and to the BIOS Makefile-flags array.
- **`os`/`io`** — not upstream `.py` at all (upstream's versions are
  `uos`-based; this build has no `MICROPY_VFS`/`uos`, Metal FS is its own
  async-shaped thing, not a mounted VFS). Metal's own
  `mods/py/stdlib_src/os/__init__.py` + `os/path.py` + `io.py`, written
  directly against five new **synchronous** host-only `pm_metal_fs_*`
  wrappers (`open`/`close`/`fread`/`fwrite`/`readdir`/`stat`/`mkdir`/
  `unlink`/`rename`, `fs.c`/`fs.h` — reusing the exact same static
  helpers the `_async` family already uses, ESP ops aren't slow enough to
  need hiding behind an `await`) exposed to Python one-function-per-op via
  `fs/fs_py_bind.c` (`pymergetic.metal.fs.*`). Bytes-only throughout
  (`MICROPY_CPYTHON_COMPAT` is off, so there's no `str.encode()`/
  `bytes.decode()` to build a text layer on top of).
  - **Bug found and fixed along the way, not pre-existing-but-known:**
    `pm_metal_esp_write_at()` (`fs/esp/esp.c`) computed `new_len = off +
    len` and then unconditionally called `pm_metal_mem_alloc(new_len,
    ...)`; for a brand-new empty file or any truncate-to-0
    (`new_len == 0`), `pm_metal_mem_alloc(0, ...)` always returns `NULL`
    (`mem.c` treats `size == 0` as invalid up front) — read as an
    allocation failure, so `io.open(path, "wb")` on a **new** path always
    failed, even though the ESP cache slot had already been created as a
    side effect (a stat/listdir right after would show the empty file
    anyway, which is what made this look like a handle-table bug at
    first, not an alloc-of-zero bug). `pm_metal_esp_write_at()` now
    special-cases `new_len == 0` before ever calling
    `pm_metal_mem_alloc`. Every `os`/`io` op — write, read back, `stat`,
    `listdir`, `mkdir`/`isdir`, `rename`, `unlink` — is boot-proofed
    end to end (`PY_PROOF_OSIO`).
- **`re`** — `extmod/modre.c` + `lib/re1.5`'s four sources
  (`charclass.c`/`compilecode.c`/`dumpcode.c`/`recursiveloop.c`).
  `MICROPY_PY_RE`/`_MATCH_GROUPS`/`_SUB` on. These four re1.5 files are
  **not** separate Sources entries — `modre.c` already `#include`s all
  four directly at its own bottom (with a preceding `#define
  re1_5_fatal(x) assert(!x)` macro they rely on), the same "glob the .c
  in" idiom `modhashlib.c` uses for its own sha256.c fallback; compiling
  them as independent translation units (tried first) double-defines
  every re1.5 symbol at link time and, standing alone outside `modre.c`'s
  textual context, also can't see `<stdbool.h>`/`MP_FALLTHROUGH` — both
  real, reproduced errors, not hypothetical. `re.match`/`.search`/
  `.split`/`.sub` and `Match.group`/`.groups` all boot-proofed
  (`PY_PROOF_RE`); `re.escape`/lookahead/inline-flag support don't exist
  in re1.5, which is exactly why `textwrap` stays Needs-glue (above).

### Trust (same as mods)

Stdlib zip is a **Mods-CA** artifact — same PKI / modes as wasm packs
([`TRUST.md`](TRUST.md)): `off` / `soft` / `enforce`.

| Rule | Lock |
|------|------|
| When | Verify **before** the zip is put on `sys.path` |
| Bad signature | **Fail closed** — do not mount; import fails |
| Missing `.sig` | Per trust mode (enforce requires it; soft/off as mods) |
| Keys | Same Mods CA pubs / `./scripts/pki` story as `mods/apps/*` |

Example artifact: `mods/py/stdlib.zip` + `stdlib.zip.sig` (build-time only —
neither is tracked in git, see "baked in, not tracked" above; the trust
check itself doesn't know or care that both were written from the embedded
copy rather than fetched or hand-placed).

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
| Full CPython / CircuitPython `uasyncio` as scheduler | Wrong shape; Metal runners own CPUs; zip `asyncio` is a thin name shim |
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

**Shipped.** Two contexts coexist, chosen per-task at spawn — not a staged
plan, real code (`py_ctx.c`/`py_ctx.h`, wired through `py.c`):

```text
MAP (many µPy blobs, one per isolated task + the one shared/default blob)
├─ shared/default context        ← immortal/frozen/code + one shared GC heap
│                                   (mp_state_ctx_default) — run-lock serialized,
│                                   unchanged from pre-isolation behavior
└─ isolated contexts (0..N)      ← each pm_metal_py_ctx_t owns its OWN
                                    mp_state_ctx_t + its OWN MAP-carved GC
                                    heap (PM_METAL_PY_ISOLATED_BLOB_BYTES,
                                    default 64 KiB) — genuinely disjoint,
                                    no shared allocator, no shared lock
```

| Metal C | µPy-shaped |
|---------|------------|
| Task / coro state | **Python task = Metal task**; `pm_metal_py_job_t.ctx` (`py_internal.h`) is `NULL` (shared) or a per-task `pm_metal_py_ctx_t*` |
| Per-CPU state switch | `mp_state_ctx` is a **per-CPU indirection macro** (`(*mp_metal_py_ctx_table[pm_metal_mem_cpu()])`, patched into vendored `py/mpstate.h`) — `pm_metal_py_ctx_enter`/`_leave` (`py_ctx.c`) point *this CPU's* table slot at the job's context around every bytecode-entry call, then restore it |
| Params / published ids | Args + Metal handles (not shared Python object graphs) |
| Stackless `await` | Python `await` → park; resume on any runner (isolated context follows the resumed task — no CPU pinning) |
| `create_task` / `await task` | Same Metal ops; thin `metal.aio` mirrors ([`COOP_MEMORY.md`](COOP_MEMORY.md)) |
| Runner stack (MAP) | Short native slice while in bytecode |

### Rules

1. **Parallel bytecode across runners is real for isolated tasks** — each owns a disjoint heap and its own `mp_state_ctx_t`, so N isolated tasks execute bytecode literally simultaneously on N CPUs; proven at boot (`PY_PROOF_PARALLEL`, below). The shared/default context (`py -c`, `py <script>` without `-x`, the REPL) still serializes through one run-lock, unchanged from before isolation existed — full N-way parallelism requires opting every task into `-x`/`_isolated`, which is a real tradeoff (own heap, own binds-reinstall cost), not a free win.
2. **No GIL API.** Exclusivity for the shared context is lock-by-invisibility: switch only at `await`; never hold a lock across `await` (same as C). Isolated contexts have no lock at all — there is never contention on a heap exactly one task owns for its whole lifetime.
3. **Mutable Python state is task-local for isolated contexts, shared for the default one.** Cross-task sharing = Metal, not shared object graphs, in both cases. Passing a live Python object between two *isolated* tasks (or between an isolated task and the shared context) is **out** — share handles, bytes copies, or published ids. `sys.argv`/`sys.modules`/`__main__.__dict__` are pinned to the shared context's storage even when read from an isolated one (a documented, narrow limitation from the static-initializer fix below — normal task code doesn't touch these).
4. **GC is per-context, not blob-wide:**
   - Isolated task at park (`await`, right before `py_ctx_leave()`) → `gc_collect()` on **its own** disjoint heap only (`py_resume_coro`, `py.c`) — no cross-task coordination needed, because no other task can be touching that heap.
   - Shared/default context keeps its pre-isolation behavior: GC runs synchronously inside the run-lock-held call, same as always.
   - **No blob-wide "all-parked compact barrier" exists or is needed** — that was the original plan's answer to a shared mutable heap; once heaps are actually disjoint, each one collects independently. (Also: stock MicroPython's `gc.c` is mark-**sweep**, never moving/compacting, so "compact" was never literally accurate for either context — corrected here.)
5. **Non-starvation:** a tight sync Python loop holds its runner (true for both context kinds). Authors must `await metal.aio.yield_()` (or equivalent) for fairness; optional later: bytecode timeslice that injects a yield. Starving FS/net/wasm on that CPU is a bug in the script or missing yield, not a second scheduler.
6. **Cancel / death:** Metal task cancel stops the Python task; uncaught exception or OOM kills **that** task only — not the blob (shared or isolated), not other runners. `py_job_release` destroys `job->ctx` (frees the whole isolated arena, one `pm_metal_mem_free`) on every teardown path — done, cancelled, excepted, or OOM'd — so isolated tasks never leak. Sync `py_call` that hits a park → hard error (never spin).

Flat shared mutable heap with N cores allocating freely was always **out**
(concurrent-GC research) — isolated contexts don't attempt that either; they
sidestep it by giving each task its **own** heap instead of sharing one.

### Task-local GC (integration contract) — shipped design

| Topic | Shipped |
|-------|------|
| Layout | Shared/default context: one MAP blob, immortal/frozen/code + one GC heap, run-lock serialized (unchanged). Isolated context: its own MAP-carved blob (`PM_METAL_PY_ISOLATED_BLOB_BYTES`, tunable per `pm_metal_py_run_*_isolated` call) + its own `mp_state_ctx_t` (heap-allocated) — created via `pm_metal_py_ctx_create`, destroyed via `pm_metal_py_ctx_destroy` |
| Alloc | Isolated task allocates only in its own heap — enforced by construction (its `mp_state_ctx` *is* that heap while it holds the CPU's table slot), not by convention |
| Cross-task | No borrowed Python refs across tasks (shared↔isolated or isolated↔isolated); Metal handles / copies only |
| Nursery GC | Isolated: `gc_collect()` on park, own heap only, invisible to the script. Shared: same synchronous-on-park behavior as before isolation existed |
| "Compact" | **Dropped as a concept** — stock `gc.c` is mark-sweep; no cross-task barrier exists because no context shares a heap with another |
| Bind reinstall | Opening an isolated context re-runs `pm_metal_py_binds_install()`/`pm_metal_py_pmcmd_install()`/`pm_metal_py_mod_install()`/`pm_metal_py_zip_init_sys_path()` against it (cheap — a linker-section walk, a few dozen `mp_store_attr`/`mp_obj_list_append` calls) — an isolated task gets `metal.*`/`pmcmd.*`/`mod.*` **and** its own `/mods/py` + `stdlib.zip` on `sys.path`, same as the shared context (`PY_PROOF_ISOLATED_STDLIB`); only `c_py_demo`'s one-off demo module stays shared-context-only (it is not stdlib, just a boot-proof fixture) |
| Caps | `pm_metal_py_isolated_ctx_count()`/`_bytes()` (`py.h`) — live count + total bytes of active isolated contexts, surfaced in the `mem` shell command's nested `py` line |

Proven at boot by `PY_PROOF_PARALLEL` (`boot_python.c`): two isolated tasks,
each a CPU-bound busy loop + a distinct global (`X = 111` / `X = 222`) +
`await a.sleep_us()` + `assert X == <its own value>` — the assertion is the
disjoint-heap proof (a shared heap would let one task's write clobber the
other's global), and the measured wall time under `-smp 4` demonstrates the
two busy loops genuinely overlapped rather than serializing.

### Cross-language GC border

A single shared tracing/moving collector across C, Python, and later
C++/Rust is **not** the right shape — their memory models are too different
to unify at that level (Python: refcount + mark-sweep; Rust: compile-time
ownership/`Drop`; C++: RAII; C: manual). What Metal needs, and already
mostly had before isolated contexts existed, is a common **teardown
boundary**, not a common **collector**:

- The unit of ownership is the Metal task/coro, not any particular
  language's heap.
- `pm_metal_async_coro_set_release(h, fn)` ([`async.h`](../include/pymergetic/metal/runtime/async/async.h))
  is that boundary — one per-coro C callback, called on teardown regardless
  of cause (done / cancelled / excepted / OOM). Every existing user
  (`http.c`, `ntp.c`, `tftp.c`, `ping.c`, `async_ops.c`, `py.c`'s
  `py_job_release`) frees everything *that subsystem* owns for that task, in
  whatever way is native to it.
- Python's isolated context plugs into this unchanged: `py_job_release`
  gained one conditional — destroy `job->ctx` if non-NULL. Destroying the
  context frees the whole per-task arena in one shot (`pm_metal_mem_free`
  on the MAP-carved blob, then the heap-allocated `mp_state_ctx_t` itself),
  no separate collection pass needed first. Python's own GC never has to
  know about C, Rust, or C++ memory at all.
- Future Rust/C++ integration follows the same border: a module that opens
  a Metal-tracked resource registers cleanup the same way; its *own*
  internal memory (Rust ownership, C++ destructors) never crosses this
  boundary at all, since it isn't Metal-tracked to begin with.

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
| Task-local GC (disjoint per-context heaps, no barrier) | See contract above |
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
5. ~~Task-local spaces (or staged path to them) + cancel/isolation; note blob size vs Doom HEAP.~~ **done** — cancel/isolation (`PY_PROOF_EXC`/`CANCEL`/`OOM`) plus genuine task-local GC spaces via opt-in isolated MicroPython contexts (`py_ctx.c`, `PY_PROOF_PARALLEL`); see [Concurrency](#concurrency--same-schematic-as-metal-async).
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

## Python REPL as the system's main shell

**Shipped.** The interactive surface a user actually types into on boot is
now a **Python REPL task** running on the one always-on shared µPy
context — same async engine as everything else, no new VM, no new
scheduler, no second interactive loop. `shell.c`'s own C line editor is
not deleted — it's the explicit, always-reachable fallback.

| Piece | Where | What |
|-------|-------|------|
| Persistent REPL task | `py.c`'s `PY_STEP_REPL` job state, `pm_metal_py_repl_start/_stop/_active/_feed_line/_prompt` (`py.h`) | One long-lived job on the shared/default context (never isolated — needs one persistent `globals` dict across every line, which the shared context already gives for free). Multi-line block detection is real `mp_repl_continue_with_input` (`MICROPY_HELPER_REPL` flipped on in `py/embed/mpconfigport.h`, Metal-owned, no patch needed), not a hand-rolled approximation — see the "join without a trailing newline, check-then-append" note in `py.c` if touching this again; getting that backwards silently breaks every `def`/`if`/`for`/`while`/`with`/`try`/`class` block after line one. Deliberately spawns straight into `PY_STEP_REPL`, bypassing `PY_STEP_ZIP` (same call shape as `pm_metal_py_fn_call_async_bound`) — `stdlib.zip`'s HTTP-fetch-then-cache is a shared-context-global one-time cost with observed multi-second worst case; whichever job resolves it first mutates `sys.path` for every shared-context job after it, REPL included, so the REPL doesn't need to trigger or await it itself. |
| Line source | `shell.c`'s committed-line path (Enter handler) | Producer/consumer single-slot mailbox (`mReplLineBuf`/`mReplLineReady` in `py.c`) — `pm_metal_py_repl_feed_line()` is the only producer, the REPL job the only consumer; SPSC handoff needs no CAS on this x86/x86_64-only target (plain write-then-flag / read-flag-then-read under TSO). `pm_metal_py_repl_feed_line()` returns -1 ("busy") if the previous line isn't drained yet — practically never hit at human typing speed against the REPL's 2 ms idle-poll, and `shell.c` prints `repl: busy, try again` rather than silently dropping the line if it ever is. |
| Prompt | `pm_metal_shell_prompt`/`MetalShellPromptAnsi` (`shell.c`) query `pm_metal_py_repl_active()`/`_prompt()` live | `>>> ` fresh statement, `... ` mid multi-line block — magenta, visually distinct from the C shell's green/blue prompt. **Deferred, not synchronous**: `feed_line()` only enqueues; the actual `>>> ` vs `... ` decision is made by the REPL job on its own next scheduler tick, not inside the Enter handler. Printing the prompt synchronously right after `feed_line()` would show the *previous* tick's stale value (e.g. `>>> ` right after typing `def f():`, before the engine noticed it needs `... `) — the Enter handler instead sets `mPromptPending` and lets the next `pm_metal_shell_poll()` tick (which runs after, not before, the async engine gets a turn) draw the prompt the line just produced. |
| `console()` escape | `shell.c`'s Enter handler, `PyReplIsQuitCall()` | Typing `console()` at `>>> ` calls `pm_metal_py_repl_stop()` and falls back to the normal C command dispatcher — `help`, `mem`, `py -f ...`, everything, unchanged. `quit()`/`exit()` are accepted as aliases — CPython/IPython's "how do I leave this REPL" muscle memory, which would otherwise just `NameError` since MicroPython defines no `quit`/`exit` sentinel objects. Call syntax only (`console()`, not bare `console`) — a bare word is ordinary (buggy) Python source, not a command, and real Python has no bare-word statement that invokes anything, so this doesn't quietly special-case what would otherwise be a `NameError`. `py -i` (see `py_shell.c`) resumes the same persistent REPL job's globals — nothing is lost switching back and forth. |
| Compat | `pmcmd.*` | Every C shell command is already a Python callable (see [Surfaces](#implementation-status)) — `pmcmd.help()` etc. work identically whether reached from the REPL or the C console. |
| Boot wiring | `boot_init.c`'s `BOOT_READY` case | After the boot tree + `metal-boot: ready` line, boot calls `pm_metal_py_repl_start()` and prints a short "Metal Python" welcome (non-fatal on failure — falls back to a plain C shell prompt exactly like before this feature existed). |
| Known limitation | `mphalport_metal.c`'s `mp_hal_stdin_rx_chr` | Stays stubbed — a script's own `input()` builtin is unsupported (REPL lines come from the shell's line queue, not from Python's own stdin read). Out of scope; revisit only if a concrete use case needs it. |

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
- [x] Task-local GC spaces + true parallel bytecode — opt-in isolated MicroPython contexts (`py_ctx.c`/`py_ctx.h`, per-CPU `mp_state_ctx` indirection patched into vendored `py/mpstate.h` via `patches/micropython/0001-metal-percpu-state-ctx.patch`): each isolated context owns its own MAP-carved GC heap + its own `mp_state_ctx_t`, no shared run-lock, `gc_collect()` on its own disjoint heap at park; `pm_metal_py_run_str_isolated`/`_run_script_isolated` (`py.h`), `py -x` shell flag, `mem`'s nested `py` line shows isolated-context count/bytes; boot-proofed disjoint-heap + concurrent-execution under `-smp 4` (`PY_PROOF_PARALLEL`). Shared/default context (unopted-in `py -c`/`py <script>`/the REPL) keeps the original run-lock-serialized behavior unchanged — see [Concurrency](#concurrency--same-schematic-as-metal-async)
- [x] Persistent Python REPL as the system's main interactive shell — see the dedicated [Python REPL as the system's main shell](#python-repl-as-the-systems-main-shell) section; boot spawns it after `BOOT_READY`, C console dispatcher stays reachable via the `console()` escape call
- [x] Real "Easy" stdlib pack (not a 1-file sample) — ~27 pure-Python micropython-lib modules in `mods/py/stdlib_src/` (`collections`+`defaultdict`, `heapq`, `bisect`, `functools`, `itertools`, `contextlib`+`ucontextlib`, `copy`, `struct`, `string`, `pprint`, `operator`, `types`, `warnings`, `errno`, `keyword`, `abc`, `quopri`, `html`, `argparse`, `stat`, `pickle`, `inspect`, `traceback`, `logging`, `base64`, `fnmatch`) + Metal's own `os`/`os.path`/`io` (written directly against `pymergetic.metal.fs`, not upstream `.py`) + four C extmods (`binascii`, `random`, `hashlib`, `re` — each needed its own build-system fix, not part of upstream's embed package at all, see [the extmods + os/io section](#random-hashlib-osio-re--three-c-extmods-one-own-pair)); `mods/py/build_stdlib_zip.sh` is a real reproducible build step (Python's own `zipfile`, `ZIP_STORED`, sorted/deterministic); `PY_PROOF_STDLIB`/`PY_PROOF_RANDOM`/`PY_PROOF_HASHLIB`/`PY_PROOF_OSIO`/`PY_PROOF_RE` import and exercise every packed module, not a 3-module sample. Second pass ([micropython-lib categorization](#micropython-lib-categorization-easy--needs-glue--defer)) needed two more self-contained grammar/builtin flag flips (`MICROPY_PY_SYS_EXC_INFO`, `MICROPY_PY_BUILTINS_STR_OP_MODULO`) plus small in-place deltas to `logging.py`/`traceback.py`/`pickle.py` (drop `time`/`io`/`sys.stderr`/bytes-encode dependencies this build doesn't have). Third pass closed `random`/`hashlib`/`os`/`io`/`re` (a `urandom`-seed C bind, a native SHA256 extmod, five new sync `pm_metal_fs` wrappers + two own `.py` files, and `modre.c`+`lib/re1.5` respectively — including a real pre-existing bug fix in `pm_metal_esp_write_at()`, see the extmods section) and, once `re` existed, `base64`/`fnmatch` promoted too with zero source changes (`MICROPY_PY_BUILTINS_BYTEARRAY` for `base64`); `textwrap`/`hmac`/`uu` stayed in Needs-glue, each for a specific, now-documented reason (re1.5 feature gap / `property` builtin / binascii has no uu functions at all)
- [x] Isolated-context ergonomics — isolated MicroPython contexts import from `stdlib.zip` too, not just `pymergetic.metal`/`pmcmd`/`mod`: `pm_metal_py_ctx_create` (`py_ctx.c`) now calls `pm_metal_py_zip_init_sys_path()` against the fresh context right after `mp_embed_init`, appending `/mods/py` + `stdlib.zip` to *that context's own* `mp_sys_path` (a genuine per-context root pointer, `MP_STATE_VM(sys_mutable[...])` — unlike `sys.argv`/`sys.modules`, it was never one of the static-initializer globals pinned to the shared context, so each context just needed its own copy of the same append call). `py_job_step`'s `PY_STEP_ZIP` no longer special-cases isolated jobs — `pm_metal_py_zip_step` is pure C (fs/http/trust, no `mp_obj_t` touch) and both context kinds now share the same one-fetch-total `g_zip_state`, so both wait on it the same way. Boot-proofed end to end (`PY_PROOF_ISOLATED_STDLIB`: an isolated task imports and exercises `heapq`)
- [x] Fatter stdlib zip — `time`/`datetime`/`hmac`/`zlib`/`gzip`/`pathlib`/`shutil`/`tempfile`/`tarfile`/`unittest`/`textwrap`/`uu` all shipped (see [Second Needs-glue pass](#second-needs-glue-pass)); `ssl` shipped too, as a genuinely Metal-flavored `pymergetic.metal.tls` async TCP+TLS client, not a CPython `ssl` shim; `threading`/`_thread` deliberately, permanently not shimmed (see above) — real gaps left: full `metal.fs`/`net`/… stdlib-shaped orchestration, native self-register (`import sample`)
- [x] `pymergetic.metal.net`/`net.http`, real on-disk `io.IOBase` streams, `json` — see [Network + FS/IO polish](#network--fsio-polish-pymergeticmetalnet-ioiobase-json): offline-safe loopback `PY_PROOF_NET` (listen/accept/connect/send/recv/dns), `gzip.open()` now wraps a real file (checkpointed inside `PY_PROOF_ARCHIVE`), `json.dumps`/`loads` checkpointed inside `PY_PROOF_FSMOD`. Deliberately not shimmed: CPython `socket`/`select`, `umqtt`/`requests`/`aiohttp`, any new VFS/mount table/per-context CWD/FS jail
