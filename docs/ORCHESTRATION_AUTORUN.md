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
14. When W9 is `DONE`, **immediately** start **W10** (registry unify +
    reconnect boot). Then W11→W15 in order. Do not stop for a new plan.
15. **Paradigm first:** new module/call style wins over `_old`/`main` shape.
    Zero human prompts; skip or script around interactive steps; log choices
    in Progress. No commits/pushes unless the owner asked.
16. Stop only when Progress has no more `TODO` (W0–W15), or the continue
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
| **W10** | Registry unify + reconnect | Always-proxy faces for floor; reconnect py/wasm/net/fs to boot |
| **W11** | upy finish + REPL shell | VM leftover rows; REPL-as-shell; async metrics + cross-lang/wasm stress |
| **W12** | gfx + drivers (Rust) | Dispatch + QEMU backends + HW ports; minimal UI; Kconfig |
| **W13** | Doc-browser | VFS source + forge render + Rust Microdot rewrite + self-serve download |
| **W14** | Signed pkg fetch | trust/ + HTTP fetch-on-miss for wasm/AOT |
| **W15** | metal-doom (sep. repo) | New branch off metal-doom `main`; adapt guest; never commit that `main` |
| **W16** | Guest dual-ABI | `guest_surface` + host WAMR natives; unblocks W15.2 (may run before finishing doom) |

This is huge. Continues grind **chunk by chunk**. Do not skip waves.
W8→W9→…→W15; if W15.2 blocked on guest ABI, do **W16** then resume W15.

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

## W10 — Registry unify + reconnect boot (after W9 DONE)

**Paradigm fixed point:** always-proxy cached-slot faces + quiesce.
`_old`/`main` = behavior only.

1. **W10.1** Unify fixed/never-unloaded floor module faces to the
   always-proxy cached-slot shape (`docs/definitions/module.md` "Two face
   shapes"). Call-compatible with existing consumers.
2. **W10.2** Reconnect `py`, `wasm`, `net`, `fs` as real deps in
   `boot/.pm/Cargo.toml`; each registers via `RegMod` /
   `pm_metal_reg_mod_load` at bring-up (same as live `dev/gfx`/`bus/pci`).
3. **W10.3** Gate: `mod check`; bios+efi build+boot; host smokes; ReadLints;
   main-branch inventory recheck → log gaps in Progress (do not expand scope).

---

## W11 — upy finish + REPL as boot shell (after W10 DONE)

Reference behavior: `_old/.../py/**`, `ORCHESTRATION_UPY_MIRROR.md` remaining
`MIRROR` rows. Shape: current `py/` / `upy/` Rust rewrite — **not** C revive.

1. **W11.1** Remaining VM: `upy/py/{lexer,parse,compile,emitbc,emitnative,emitnx64,frozenmod,repl}.rs` (finished only).
2. **W11.2** `py/loop.rs` + console/`mphalport` wiring.
3. **W11.3** REPL-as-boot-shell (shared/default context only; **no**
   `pmcmd` / command-registration shell — REPL is the only interactive
   surface; reg later via the Python module tree).
4. **W11.4** `py/.pm/Kconfig` (heap/stack/features).
5. **W11.5** Async concurrency test + metrics: many Python async tasks;
   prove **1 runner/core**; log tasks/awaits/per-runner progress/starve gaps.
6. **W11.6** Cross-language call stress under load: C/RS/PY/wasm all
   directions (incl. wasm↔wasm via dynamic imports, host↔guest) through
   real proxy/`fwd_native`/trampoline paths; unload/reload under traffic
   where safe; fail on wrong results/hangs/quiesce stalls.
7. Gate: bios+efi REPL prompt; proofs + stress metrics in Progress;
   `mod check`; lints; main recheck log.

---

## W12 — gfx + drivers Rust (after W11 DONE)

Reference behavior: `_old/.../dev/gfx/**`. Shape: Rust `dev/gfx` module style.

1. **W12.1** Port dispatch (`gfx` compositor/surfaces/present, `scanout`
   probe order) — replace stub detect.
2. **W12.2** QEMU backends: bochs, virtio_gpu, gop_blt, lfb_copy.
3. **W12.3** HW ports i915_855gm / radeon_rv370 (unverified; flag in Progress).
4. **W12.4** Minimal new Rust UI/console consumer (not full `_old/shell/ui`
   revival) + `.pm/Kconfig` per backend.
5. Gate: bios+efi present on QEMU display; `mod check`; lints; main recheck.

---

## W13 — Doc-browser + Rust Microdot rewrite (after W12 DONE)

1. **W13.1** Extend `/src` VFS for source browse; live `reg` symbol reflection.
2. **W13.2** Forge `metal`-port in-memory render (`pm_metal_forge_render` or
   equiv) — templates + src input; no pre-baked face blob required.
3. **W13.3** Rust rewrite of the Microdot **pattern** (route table, method+path
   dispatch, request/response) under `net/http/microdot`; page set as
   consumers (home/docs/iface/symbols/externals/limits). Not Python httpd.
4. **W13.4** Self-serve download: running kernel image bytes + loaded wasm
   module bytes as `application/octet-stream`.
5. Gate: loopback GET browse + symbols + both downloads; `mod check`; lints;
   main recheck.

---

## W14 — Signed wasm/AOT package fetch (after W13 DONE)

Reference behavior: `main` `guest/pkg` + `trust/`. Shape: new modules.

1. **W14.1** `trust/` verify-only (reuse already-vendored crypto; no second
   lib). Policy `off`/`soft`/`enforce` via Kconfig.
2. **W14.2** Package fetch-on-miss over `net/http` client into wasm load
   path (lazy; never auto-fetch at boot); verify before WAMR.
3. Non-interactive test keys only for smoke.
4. Gate: host smoke fetch→verify→load→call; bios+efi; lints; main recheck.

---

## W15 — metal-doom validation (after W14 DONE; separate repo)

Repo: `packages/metal-doom` (sibling checkout). **Hard:** create a **new
branch off its `main`** before any edit; never commit that `main`; no push
unless owner asks; never fold sources into `packages/metal`.

1. Diff glue vs new gfx/input/sound/net guest ABI; adapt on the new branch.
2. Rebuild via its `./scripts/build.sh` against sibling `../metal`.
3. Verify tab/present/pace; use existing `DOOM_ASYNC.md` / `DOOM_PERF.md`
   if regressions — do not rediscover.
4. If `setup-doomgeneric.sh` prompts with no non-interactive path → skip
   wave, log why in Progress.
5. If guest dual-ABI headers/natives missing → **W16 first**, then resume.

---

## W16 — Guest dual-ABI (unblocks W15.2)

Paradigm: `.pm/module` `"guest_surface": true` → C face forks
`PM_METAL_PKG_IMPORT`; host registers WAMR natives at
`pm_metal_wasm_port_init` (`wasm/port/host_natives.c`). Kernel imports are
**not** `pm_metal_imports` fwd (that path is guest↔guest only).

1. **W16.1** `log` guest_surface + host native + `tests.wasm_guest_log`
   pack; boot mark `guest surface ok`.
2. **W16.2** async time/sleep/yield/await + mem cookie dual-ABI as needed.
3. **W16.3a** fs + gfx (`read_mem`/`write_mem`/`blit`/`present_async`)
4. **W16.3b** shell_log + input poll/lock
5. **W16.3c** guest coro pin/step + process crown
6. **W16.3d** audio (null backend finished; optional virtio)
7. Gate each slice: pack + bios+efi; then resume W15.2.

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
After W7 DONE → W8; …; after W9 DONE → W10 → … → W15 — no new briefing.  
Never skip the “last part tidy” recheck between continues.
