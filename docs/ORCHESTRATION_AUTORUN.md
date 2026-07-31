# Orchestration autorun (unattended)

**Audience:** agent continues while the owner is AFK.  
**Do not ask the user.** Do not wait on approvals. Do not re-open Locked
decisions in [`ORCHESTRATION.md`](ORCHESTRATION.md). If stuck on a
non-policy question, pick the option that matches Locked #1–#10 and keep
going. **Always:** async-first OS (Locked #5) — design for Metal await/runners,
not sync-primary + async later.

**Canon:** [`ORCHESTRATION.md`](ORCHESTRATION.md) Locked table +  
[`ORCHESTRATION_UPY_MIRROR.md`](ORCHESTRATION_UPY_MIRROR.md).

**Progress file (update every chunk):**  
[`ORCHESTRATION_PROGRESS.md`](ORCHESTRATION_PROGRESS.md)

---

## Standing orders

1. Read Locked #1–#10 in ORCHESTRATION.md once per continue; obey them.
   Especially **#5 async-first OS** on every API (py, wasm, net, ssh, http).
2. Open `ORCHESTRATION_PROGRESS.md` → do the first `TODO` chunk → mark
   `DONE` / `BLOCKED` with one line why → commit **only if** the user
   previously asked for commits (default: **no commits**).
3. Never invent hollow stubs. Prefer a small finished slice over 50 empty
   `.rs` files. Never ship a sync-only primary path when the feature is I/O.
4. Never delete or rewrite module `.gitignore` on clean; sync owns ignore.
5. Never `#include` upstream `external/micropython/py/*.h` from Metal Rust
   once a face exists under `upy/`.
6. After every **gate chunk**: run the Gate commands below. BIOS + EFI both.
7. **End of every continue:** run **Full lints** (below) on everything touched
   this chunk. Lint failure = chunk not DONE; fix before the next chunk.
8. **Start of every continue:** re-check the **previous** chunk is really
   complete and tidy (files present, no stubs, gates/lints still hold). If
   not, finish/tidy that first — do not advance the queue.
9. Tools allowed: repo shell, forge-cli, git status/diff/log, ReadLints (no
   push, no config, no commit unless user asked). No interactive prompts.
10. If a gate fails: fix it in-session before starting the next chunk.
11. If vendor clone needs network and fails: mark `BLOCKED: network` and
    continue with the next chunk that does not need that vendor.
12. When W0–W7 are all `DONE`, **immediately** start **W8 Dropbear SSH**
    (live dirs already under `src/.../net/ssh/`). Do not stop for a new plan.
13. When W8 is `DONE`, **immediately** start **W9** — finish `net/http`
    (ASGI server + Microdot runner under the same module tree). Do not stop
    for a new plan.
14. Stop only when Progress has no more `TODO` (W0–W9), or the continue
    budget is exhausted mid-chunk (leave Progress honest).

---

## Waves (do in order)

| Wave | Name | Exit criteria |
|------|------|----------------|
| **W0** | Prep / tooling sanity | `mod check` green; bios+efi still build; Progress file live |
| **W1** | `reg/` finished | Module exists; register/lookup/ptr-bind work; smoke; faces sync; bios+efi link if wired |
| **W2** | Vendor + `py/` Metal edge | `external/micropython` present; `py/` hub + loop/alloc/gc_off/async_bridge/bind/port skeleton **finished enough to link or clearly deferred with real stubs omitted** |
| **W3** | Upy mirror (inventory) | Grind `MIRROR`/`REWRITE` rows in priority bands below; no hollow files |
| **W4** | Upy bring-up proofs | Serial `print` / await park on **bios and efi** |
| **W5** | `wasm/` host | Load image → `reg`; one Metal memory; bios+efi |
| **W6** | Packs | `tests/` `type=package` C **and** Rust → `.wasm`; load/call; bios+efi |
| **W7** | Wire | Kernel auto-register into `reg`; face check still green |
| **W8** | Dropbear SSH | Finish `net/ssh/` (dirs exist); port from `_old`; bios+efi proof |
| **W9** | `net/http` complete | Finish ASGI under `http` + Microdot runner there; faces/`reg`; proof |

This is huge. Continues grind **chunk by chunk**. Do not skip waves.
W8 after W7, W9 after W8 — same autorun, no new briefing.

---

## Full lints (every continue, after the chunk)

From package root. Any failure → fix before marking DONE.

```bash
./forge-cli mod check
# Must return nothing:
grep -rlE '#include\s*<(Uefi\.h|Library/|Protocol/|IndustryStandard/)' \
  src/pymergetic/metal --include='*.c' --include='*.h' || true
grep -rnP '"([^"\\]|\\.)*[→—–…‘’“”✓×°±]([^"\\]|\\.)*"' src --include='*.c' || true
```

Also:

1. **ReadLints** on every path touched this chunk (C/h/rs under Metal).
2. Forge Rust changes: `./forge-cli version` (release build) must succeed.
3. Include / `.clangd` / CDB changes: fix flags; note “restart clangd” in
   Progress (owner AFK — still leave the tree correct).

---

## Gate commands (non-interactive)

Run from package root `packages/metal`:

```bash
./forge-cli mod sync
./forge-cli mod check
./forge-cli build bios
./forge-cli build efi
# When run proofs are in Progress for this chunk:
timeout 90 ./forge-cli run bios || true
timeout 90 ./forge-cli run efi || true
```

- Prefer `timeout` so QEMU cannot hang the continue forever.
- Treat build failure as hard fail; run timeouts: capture serial log, decide
  from banner/`ready` / proof string in Progress.
- `mod clean` = faces only; **never** touch `.gitignore`.

---

## Chunk size

One continue ≈ **one Progress chunk** (or until ~gate).  
Examples of one chunk:

- Create `reg/` module + `__init__.rs` border + table + smoke  
- Vendor micropython submodule  
- Land `py/alloc.rs` + `upy/py/malloc.rs` wired to `pm_metal_mem_*`  
- Rewrite one inventory band (e.g. all `upy/py/objects/objbool`… small set)  
- Add `tests/py_smoke` + wire into boot when ready  

Do not start five chunks in parallel across waves.

---

## W1 — `reg/` (first real code)

Deliverables under `src/pymergetic/metal/reg/`:

```text
.pm/module          # type=module, name=pymergetic.metal.reg, impl=rs
.pm/Cargo.toml
.pm/smoke.rs        # register + bind + call
__init__.rs         # pm_metal_reg_register / lookup / bind / call-by-name
table.rs
bind.rs
publish.rs          # optional helper
```

API shape (Locked #2, #6):

```text
pm_metal_reg_register(full_module, func, fn_ptr, ...)
pm_metal_reg_lookup(...)
pm_metal_reg_bind(...) -> ptr handle
# convenience call-by-name OK for smoke
```

Keys: full module string always. Flat map OK.  
Gate: `mod sync` + `mod check`; host smoke; bios+efi still build (link reg
when boot needs it — can be linked unused first if safer).

---

## W2 — Vendor + Metal `py/` edge

1. Add `external/micropython` (submodule or documented pin). If network
   blocked → `BLOCKED` and skip to any remaining W1 polish.
2. Create `src/pymergetic/metal/py/` module (`impl=rs`) with **Metal edge
   only** first (not the whole inventory):

```text
__init__.rs loop.rs async_bridge.rs step.rs bind.rs handle.rs
alloc.rs gc_off.rs libc_policy.rs
port/{mpconfigport.h,mphalport.h,mphalport.c,stubs.c}
upy/   # empty dirs ok; files only when finished
```

3. `libc_policy`: default **Metal libc** until upy shared libc is proven.  
4. Gate: sync/check; bios+efi build (upy may be behind a cfg flag until
   link-ready — prefer cfg over fake symbols).

---

## W3 — Inventory grind (priority bands)

Work only `MIRROR` / `REWRITE` rows from
[`ORCHESTRATION_UPY_MIRROR.md`](ORCHESTRATION_UPY_MIRROR.md).  
Skip `OMIT_*` / `DEAD` / `TOOL` / `SKIP_VENDOR`.

**Band order (do not reshuffle):**

| Band | Scope |
|------|--------|
| B0 | `mpconfig`, `misc`, `mpstate` (no isolation), `qstr`, `obj` core header faces |
| B1 | `malloc` → Metal alloc; `gc` DEAD paths; `runtime` / `vm` / `bc` skeleton |
| B2 | `objects/` essential: none, bool, int, str, list, dict, tuple, type, except, fun, module |
| B3 | `builtin/`: modbuiltins, modsys, moderrno, builtinimport (metal/reg hooks) |
| B4 | Remaining `objects/` + `builtin/` |
| B5 | `extmod/` MIRROR keep-list (json, os, time, re, …) + thin vfs → Metal fs |
| B6 | `extmod/asyncio/` REWRITE → Metal async_bridge (not uasyncio scheduler) |
| B7 | `shared/` SHARED_OPT only as needed by above |

Per file: implement finished Rust **or** leave the inventory row untouched.
Update Progress with stem names completed.

Gate every ~band: `mod check`; bios+efi build; when interpreter can run,
bios+efi run proofs.

---

## W4 — Proofs

`tests/py_smoke/` (app `.py` OK):

- `print("ok")` on serial  
- `await` sleep/yield parks a Metal task  

Must pass **bios and efi** (`timeout` wrapped).

---

## W5–W6 — Wasm

1. Vendor `external/wamr` (or Locked-compatible runtime).  
2. `src/.../wasm/` host: load → register exports into `reg`; **one Metal
   memory**.  
3. `tests/wasm_hello` (`impl=rs`) + `tests/wasm_hello_c` (`impl=c`) packs.  
4. Forge pack path for both languages.  
5. Gate bios+efi load/call.

---

## W7 — Wire

- Kernel module borders auto-publish to `reg` at bring-up (or sync-time
  table).  
- `mod check` still green.  
- Optional: light perf numbers recorded in Progress.

---

## Explicit non-goals during autorun

- Do not revive `_old` FRESH/SHARED / py_ctx isolation  
- Do not implement importlib resolve before wasm loads  
- Do not commit unless the user asked  
- Do not push  
- Do not “fill” inventory with empty `todo!()` modules  

---

## W8 — Dropbear SSH (after W0–W7 DONE)

Live placeholders already exist:

```text
src/pymergetic/metal/net/ssh/
  dropbear_metal/          # .gitkeep today — port options/config/io from _old
  dropbear_stubs/          # arpa/ netinet/ sys/ — restore freestanding stubs
```

Reference (do not link `_old` forever):  
`_old/src/pymergetic/metal/net/ssh/` (`ssh_dropbear.*`, `dropbear_fd.*`,
`dropbear_posix.c`, stubs, `dropbear_metal/*`),  
`_old/patches/dropbear/`, `_old/scripts/setup.d/deps/dropbear.sh`.

Deliver:

1. Vendor/pin Dropbear under `external/` if needed (same style as other
   externals); Metal border under `net/ssh/`.
2. Restore stubs + metal port; ISO C / stdint in shared Metal (EDK2 only
   under `src/efi/**` / `src/bios/**` if required).
3. Wire listen/accept into Metal net + **async first** (coro/await; one
   memory, no special heap). Sync-only SSH path = not DONE.
4. Register useful entrypoints on `reg` under full names
   (`pymergetic.metal.net.ssh…`).
5. Gate: bios+efi build; proof (serial log and/or connect) with `timeout`.

Steal finished mechanics from `_old`; do not dump unfinished stubs as DONE.

---

## W9 — `net/http` feature-complete (after W8 DONE)

**Goal:** finish **`pymergetic.metal.net.http`** as one umbrella:

```text
net/http/
  __init__.rs     # client GET          (live)
  server.rs       # ASGI listen/mount   (live C mounts; finish from _old asgi)
  microdot/       # ASGI app runner     (W9 — Rust, faces for all langs)
```

Microdot is still **only** the ASGI runner leaf (route/dispatch ->
`conn_*` / `reply`). It is **not** a second TCP/HTTP server. Putting it
under `http/` is layout: client + ASGI + Microdot = HTTP feature-complete.
Do **not** revive a sibling `net/asgi/` package — live ASGI is `http/server`.

Reference: live `net/http/{__init__,server}.rs`, `_old/.../net/asgi/` (WS,
Py/Wasm runners, config to fold into `server` / siblings),
`_old/mods/httpd/` (`handle` -> Microdot -> reply),
`_old/typings/microdot/`, upstream microdot as **API shape**.

```text
src/pymergetic/metal/net/http/
  .pm/...                  # pymergetic.metal.net.http
  __init__.rs              # pm_metal_net_http_*          (client)
  server.rs                # pm_metal_net_http_server_*   (ASGI)
  microdot/
    .pm/module             # pymergetic.metal.net.http.microdot, impl=rs
    __init__.rs            # App/route/request/response + leaf handle
```

Rules:

- ASGI (`server`) owns sockets; Microdot only dispatches/replies through it.
- **Async-first:** leaf `handle` / routes are Metal-await coroutines (same
  as old async `httpd.handle`), not a blocking sync server loop.
- Rust Microdot = runner/API via lang-pool + `reg` (full names under
  `...net.http.microdot...`).
- Gate: `mod check`; bios+efi; mount + one GET/reply smoke with `timeout`.

Do not treat "zip microdot onto ESP" as W9 DONE.

---

## Continue protocol

Each user `continue` message means:

1. **Re-verify the last `DONE` chunk** (or last `IN_PROGRESS` if any):
   - Re-read what Progress claims was finished  
   - Confirm files exist, no hollow stubs, gates still green for that slice  
   - Run **Full lints** on those paths again  
   - If anything is messy/incomplete: fix it and keep that chunk as the
     work — do **not** start a new TODO until the last part is tidy  
2. Open Progress → find first `TODO` (only after step 1 is clean)  
3. Execute that chunk + its gate  
4. **Full lints** on touched paths; fix until clean  
5. Update Progress (mark DONE only if tidy + gates + lints OK)  
6. If time remains and last part is tidy → next `TODO`  
7. End turn with a **short** status: last-chunk recheck OK/fixed, wave,
   chunk id, DONE/BLOCKED, lint OK, next TODO  

Owner may enqueue many continues; Progress is the only handoff.  
After W7 DONE → W8; after W8 DONE → W9 — no new briefing required.  
Never skip the “last part tidy” recheck between continues.
