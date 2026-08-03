# Orchestration progress (autorun handoff)

Updated by the agent every chunk. Owner: enqueue `continue` while AFK.  
Runbook: [`ORCHESTRATION_AUTORUN.md`](ORCHESTRATION_AUTORUN.md).  
Locks: [`ORCHESTRATION.md`](ORCHESTRATION.md).

**Last agent note:** Autorun queue empty (W0–W16 all DONE). Removed
app-specific `wasm/_doom.rs` + bringup doom proof (SOURCETREE: no
app-specific code in Metal; external apps via `METAL_EXT_APPS` only).
Stop until owner adds new Progress TODOs.

---

## Wave status

| Wave | Status | Notes |
|------|--------|-------|
| W0 Prep | `DONE` | sync/check + bios/efi build green |
| W1 reg/ | `DONE` | register/lookup/bind/call0; smoke ok; faces sync |
| W2 Vendor + py edge | `DONE` | micropython v1.28.0 submodule; py edge smoke ok |
| W3 Inventory B0–B7 | `DONE` | B0–B7 finished subsets; no hollow stubs |
| W4 Py proofs | `DONE` | print `ok` + await park `await ok` bios+efi |
| W5 wasm host | `DONE` | WAMR load->reg; one Metal mem; bios+efi |
| W6 packs C+rs | `DONE` | forge pack rs+c; load/call bios+efi `wasm pack ok` |
| W7 wire | `DONE` | publish_kernel; faces check; bios+efi proofs |
| W8 Dropbear SSH | `DONE` | listen :22 dropbear; reg bind; bios+efi proofs |
| W9 http complete | `DONE` | listen :80 + loopback GET /health ok; microdot |
| W10 Registry unify + reconnect | `DONE` | always-proxy + product boot bios+efi ready ok |
| W11 upy + REPL + stress | `DONE` | VM+REPL+Kconfig+concurrency metrics+cross-lang/wasm stress; native emit deferred (bytecode owns exec) |
| W12 gfx + drivers | `DONE` | QEMU backends + HW ports (unverified) + UI stripe + Kconfig; bios+efi present |
| W13 Doc-browser | `DONE` | VFS + forge render + Microdot browse + self-serve downloads |
| W14 Signed pkg fetch | `DONE` | trust/ + load_verified + HTTP fetch-on-miss |
| W15 metal-doom | `DONE` | doom.wasm + host callin pace gate bios+efi; shell tab later |
| W16 Guest dual-ABI | `DONE` | W16.1–W16.3d; all guest surfaces for doom |

Status values: `TODO` | `IN_PROGRESS` | `DONE` | `BLOCKED`

**Per continue:** (1) tidy-check last DONE -> (2) chunk -> (3) gate ->
(4) **full lints** -> (5) update this file.  
**Always:** async-first OS (ORCHESTRATION Locked #5).

---

## Chunk queue (first unfinished wins)

- [x] **W0.1** `./forge-cli mod sync && ./forge-cli mod check` green  
- [x] **W0.2** `./forge-cli build bios` + `./forge-cli build efi` green  
- [x] **W1.1** Create `src/pymergetic/metal/reg/` `.pm/module` + Cargo + `__init__.rs` border  
- [x] **W1.2** `table.rs` + `bind.rs` (full module names; flat map)  
- [x] **W1.3** `.pm/smoke.rs` register/lookup/bind  
- [x] **W1.4** `mod sync` + `mod check`; bios+efi still build  
- [x] **W2.1** Vendor `external/micropython` (submodule @ v1.28.0)  
- [x] **W2.2** Create `py/` Metal edge (alloc/gc_off/async_bridge/bind/libc_policy/port) — finished only; no hollow loop  
- [x] **W2.3** Gate: sync/check/bios/efi + py host smoke  
- [x] **W3.B0** Inventory band B0 (mpconfig/misc/mpstate/qstr/qstrdefs/obj)  
- [x] **W3.B1** Band B1 (malloc->Metal, gc dead, runtime/vm/bc)  
- [x] **W3.B2** Band B2 (essential objects)  
- [x] **W3.B3** Band B3 (core builtins + import->reg)  
- [x] **W3.B4** Band B4 (rest objects/builtins — finished subset)  
- [x] **W3.B5** Band B5 (extmod keep-list)  
- [x] **W3.B6** Band B6 (asyncio REWRITE -> Metal async)  
- [x] **W3.B7** Band B7 (shared as needed)  
- [x] **W4.1** `tests/py_smoke` print proof bios+efi  
- [x] **W4.2** await/park proof bios+efi  
- [x] **W5.1** Vendor wamr (or chosen runtime)  
- [x] **W5.2** `wasm/` host load->reg; one memory  
- [x] **W5.3** Gate bios+efi  
- [x] **W6.1** `tests/wasm_hello` impl=rs pack  
- [x] **W6.2** `tests/wasm_hello_c` impl=c pack  
- [x] **W6.3** Forge pack both; load/call bios+efi  
- [x] **W7.1** Kernel auto-register into reg  
- [x] **W7.2** Final mod check + bios+efi + note perf if any  
- [x] **W8.1** Inventory `_old` ssh/dropbear -> live `net/ssh/` plan (no hollow copy)  
- [x] **W8.2** Restore `dropbear_stubs/` + `dropbear_metal/` from `_old` (finished port)  
- [x] **W8.3** Wire Metal ssh border + async/net; `reg` full names  
- [x] **W8.4** bios+efi build + listen/proof (`timeout`); full lints  
- [x] **W9.1** Finish `net/http/server` ASGI (fold needed `_old` asgi: Py/Wasm/WS/config)  
- [x] **W9.2** Add `net/http/microdot/` runner (`impl=rs` App/route + leaf `handle`)  
- [x] **W9.3** `mod sync` faces + `reg` full names; smoke  
- [x] **W9.4** bios+efi mount + GET/reply proof; full lints  
- [x] **W10.1** Unify floor-module faces to always-proxy cached-slot shape
- [x] **W10.2** Reconnect `py`/`wasm`/`net`/`fs` in `boot/.pm/Cargo.toml` + reg bring-up
- [x] **W10.3** Gate: mod check; bios+efi; smokes; lints; main recheck log
- [x] **W11.1** upy VM leftovers — lexer+parse+compile/emitbc DONE; `frozenmod.rs` (string-only registry) + `repl.rs` (`continue_with_input` + `exec_line`) DONE; `emitnative.c`/`emitnx64.c` deliberately OMITted (bytecode VM owns W11 execution; native emit deferred until measured need)  
- [x] **W11.2** `py/_loop.rs` (cooperative REPL loop, C ABI, reg bind) + `py/port/mphalport.c` wired to `dev.stream`/`log`/`async/time`; `py/.pm/build.rs` compiles it into every target; smoke B12; bios+efi green  
- [x] **W11.3** UART RX (`try_getc` bios+efi + `dev.serial` wrapper) + REPL-as-boot-shell (`py/_shell.rs`, async task off `bringup.c`); smoke B13; bios+efi green; verified interactively in QEMU. **No pmcmd** — owner ruled out command-registration / shell meta-lines; REPL only (reg later via Python module tree).  
- [x] **W11.4** `py/.pm/Kconfig` (`CONFIG_PM_METAL_PY_ENABLE_REPL_SHELL`, real `#ifdef` in `bringup.c`; heap/REPL-buffer/DYNAMIC_COMPILER documented as not-yet-Kconfig-driven); sourced from `config/metal/Kconfig`; `defconfig`/`.config` updated  
- [x] **W11.5** upy async concurrency test + 1-runner/core metrics (`async` metric C ABI; `pm_metal_py_proof_concurrency`; smoke B14; boot-tree `concurrency ok`; bios QEMU `runners=4` equal step spread)  
- [x] **W11.6** Cross-lang call stress under load (`pm_metal_wasm_proof_stress` + host→wasm `reg_call0`; UP quiesce poll fix; host wasm smoke + bios QEMU)  
- [x] **W11.7** Gate: REPL bios+efi; stress metrics; mod check; lints; main recheck  
- [x] **W12.1** Rust gfx/scanout dispatch (replace stub) — `_scanout` + compositor; `detect` harvests+presents  
- [x] **W12.2** QEMU backends: bochs / virtio_gpu / gop_blt / lfb_copy — all present-ok paths verified  
- [x] **W12.3** HW ports i915_855gm / radeon_rv370 (unverified on QEMU; real C ports + Rust Ops)  
- [x] **W12.4** Minimal Rust UI stripe + Kconfig; bios+efi `gfx present` + `gfx ui stripe ok`  
- [x] **W13.1** `/src` VFS browse + live reg symbol reflection  
- [x] **W13.2** forge metal-port in-memory face render (`pm_metal_forge_render`)  
- [x] **W13.3** Microdot doc-browser routes (`/` `/symbols` `/src`) + `doc browse ok`  
- [x] **W13.4** Self-serve `/download/kernel` + `/download/wasm` (loaded image/wasm)  
- [x] **W13.5** Gate: `doc browse ok` includes downloads; mod check; bios+efi ready  
- [x] **W14.1** `trust/` EdDSA verify + Kconfig off/soft/enforce (`trust verify ok`)  
- [x] **W14.2** `load_verified` + `fetch_register` + `/pkg/` static; verify before WAMR  
- [x] **W14.3** Gate: `wasm fetch ok` bios+efi; mod check; lints  
- [x] **W15.1** Branch `w15-adapt-metal-abi` off `main` in `packages/metal-doom`  
- [x] **W15.2** metal-doom adapted on sibling branch (`w15-adapt-metal-abi`); Metal hosts generic load/callin/guest coro only — **no** app-specific `_doom.rs` / bringup proof in tree (SOURCETREE: Metal carries no app-specific code; stage via `METAL_EXT_APPS`)
- [x] **W15.3** Skip/blocked logged: no `pymergetic/metal/runtime/mem/mem.h` (etc.) in live include/  
- [x] **W16.1** `log` guest_surface + `host_natives.c` + `tests.wasm_guest_log`; `guest surface ok`  
- [x] **W16.2** async.time sleep/yield/mono + mem cookies; `tests.wasm_guest_async`; `guest async ok`  
- [x] **W16.3a** fs + gfx guest surfaces (`read_mem`/`write_mem`/`blit_bgra`/`present_async`); `guest fs|gfx ok`  
- [x] **W16.3b** shell_log + input poll/lock (`push_key`/`poll_key_event`); `guest shell|input ok`  
- [x] **W16.3c** guest coro pin/step + process crown; `guest coro|process ok`  
- [x] **W16.3d** audio guest surface (null backend; virtio optional later)  


---

## Main-branch gap log (recheck after each wave)

| After | Candidate (do not auto-expand) | Notes |
|-------|--------------------------------|-------|
| W10.3 | py full `RegMod` catalog (still `py_bind_reg` late table) | W11 owns REPL/bind surface |
| W10.3 | gfx/UI still stub vs old product scanout | W12 |
| W10.3 | doc-browser / Microdot self-serve | W13 |
| W10.3 | signed wasm fetch-on-miss | W14 |

---

## Inventory tally (W3)

| Band | MIRROR/REWRITE done | Notes |
|------|---------------------|-------|
| B0 | 6 | mpconfig misc mpstate qstr qstrdefs obj |
| B1 | 6 | malloc gc(DEAD) bc0 bc runtime vm |
| B2 | 11 | objects/{none,bool,int,str,list,dict,tuple,type,except,fun,module} |
| B3 | 4 | builtin/{moderrno,modsys,modbuiltins,builtinimport->reg} |
| B4 | 12 | float set range slice cell array singleton object + math/array/collections/help |
| B5 | 11 | json binascii heapq random time platform os re vfs/modvfs misc; thin->fs |
| B6 | 8 | asyncio/{core,task,event,lock,funcs,uasyncio} + modasyncio |
| B7 | 2 | shared/runtime/{pyexec,softtimer}; Metal libc; no hollow shared libc |

---

## Blockers

- (none for W15 host pace gate; product shell `run`/`tab` still absent)

---

## Log (newest first)

- 2026-08-02 — Autorun STOP: no Progress TODO left (W0–W16 DONE).
  W15.2 tidy-recheck: bios `doom load ok` + `metal-doom: ok` +
  `doom pace ok` + ready; IDE lints clean on doom/wasi/async/fs paths.
- 2026-08-02 — W15.2 DONE: BIOS load window 64MiB (mods+IWAD fit);
  await result copy on wake; doom eager-await→yield; WASI stubs for
  wasi-libc guests; MAX_HANDLES 512. Gate bios+efi with
  `METAL_EXT_APPS=doom=…`: `doom load ok` + `metal-doom: ok` +
  `doom pace ok` + ready. Shell tab still future. Next: W15 wrap /
  owner direction.
- 2026-08-02 — W15.2 progress: `metal-doom` `metal_abi.h` maps live
  `dev.*` / async / fs / shell; cookie mem + `copy_out_at` native; local
  stubs for UI/process/mod/net.ip. `./scripts/build.sh` ->
  `build/doom/doom.wasm`. Next: host load + present/pace gate.
- 2026-08-02 — W16.3d DONE / W16 wave DONE: `dev/audio` null backend
  (`pm_metal_dev_audio_*`); guest_surface + host natives; pack
  `tests.wasm_guest_audio`. Gate bios+efi `guest audio ok` + ready.
  Next: W15.2 doom glue adapt (`dev.*` prefixes, guest coro, audio).
- 2026-08-02 — W16.3c DONE: `wasm/port/guest_coro.c` create/pin/step
  trampoline (own exec_env); `coro_alloc` on engine; process natives;
  pack `tests.wasm_guest_coro`. Gate bios+efi `guest coro ok` +
  `guest process ok` + ready. Next: W16.3d audio or W15.2 adapt.
- 2026-08-02 — W16.3b DONE: `shell/` `pm_metal_shell_log`; input HID
  key/pointer rings + PS/2 drain + poll/lock/push_key; guest_surface
  + host natives; pack `tests.wasm_guest_input`. Gate bios+efi
  `guest shell ok` + `guest input ok` + ready. Next: W16.3c coro
  session/pin + audio/process.
- 2026-08-02 — W16.3a DONE: mem guest cookies in mem/; fs
  `read_mem`/`write_mem` + guest_surface; gfx `blit_bgra` +
  `present_async` + guest_surface; host natives under `fs` /
  `dev.gfx`; pack `tests.wasm_guest_surfaces`. Gate bios+efi
  `guest fs ok` + `guest gfx ok` + `guest surface ok`. Next: W16.3b
  input/audio/shell/process/coro.
- 2026-08-02 — W16.2 DONE: `pm_metal_async_sleep`/`yield`/`mono_ms` on
  async.time; guest_surface async+mem; host natives registered under
  stem modules (`async.time`/`await`/`handle`/`task`/`mem`); cookie
  alloc/copy_in/out; pack `tests.wasm_guest_async`. Gate bios+efi
  `guest async ok` + `guest surface ok`. Next: W16.3.
- 2026-08-02 — W16.1 DONE: log `guest_surface`; `host_natives.c`
  registers `pm_metal_log` at WAMR init; import-fwd skips
  `pymergetic.metal.*`; pack `tests.wasm_guest_log` logs `guest log ok`.
  Gate bios+efi `guest surface ok` + ready. Force-sync needed once for
  face fork. Next: W16.2 async/mem.
- 2026-08-02 — W16 opened (unblocks W15): `guest_surface` on log;
  `wasm/port/host_natives.c` registers `pm_metal_log`; skip
  `pymergetic.metal.*` in import fwd; pack `tests.wasm_guest_log`.
- 2026-08-02 — W15.1 DONE / W15.2 BLOCKED: created `w15-adapt-metal-abi`
  off metal-doom `main` (no commits). `./scripts/build.sh` fails: missing
  `pymergetic/metal/runtime/mem/mem.h` and the rest of the old dual-ABI
  guest surface in live include/. dg+WADs present; setup not the issue.
  Next: Metal guest WASI restore, or owner guidance — then resume W15.2.
- 2026-08-02 — W14.2+W14.3 DONE / W14 wave DONE: `pm_metal_wasm_load_verified`
  (trust_accept then load); freestanding `pm_metal_wasm_fetch_register`
  (HTTP GET heap buf + load_verified); microdot `/pkg/tests.wasm_hello.wasm`;
  proof signed local load + unsigned loopback fetch + call ready. Gate
  bios+efi `wasm fetch ok` + ready. Next: W15 metal-doom new branch.
- 2026-08-02 — W14.1 DONE: `trust/` (impl=c) Monocypher `crypto_eddsa_check`;
  `verify_mods` / `accept_mods` + pubkey set; Kconfig choice default soft;
  PRODUCT_COMMON unit; boot `pm_metal_trust_proof` (baked test vector, no
  private key in image). Gate bios+efi `trust verify ok` + ready. clangd
  `-Iexternal/monocypher/src`. Next: W14.2 fetch-on-miss.
- 2026-08-02 — W13.4+W13.5 DONE / W13 wave DONE: `pm_metal_net_http_send_bytes`;
  `/download/kernel` serves loaded image head (BIOS `METL` bootinfo, EFI
  `MZ`); `/download/wasm?name=`; wasm `pm_metal_wasm_image`. Proof needles
  METL|MZ|ELF + wasm `asm`. Gate bios+efi `doc browse ok` + ready; mod check.
  Next: W14.1 trust/.
- 2026-08-02 — W13.3 DONE: microdot `_pages` home/symbols/src; server
  keeps `?query` on `conn_target` (mount match strips); src cat capped
  1KiB so listen handler does not stall loopback; proof closes each
  GET coro. Gate bios+efi `doc browse ok` + `ready ok`. Next: W13.4.
- 2026-08-02 — W13.2 DONE: `pm_metal_forge_render(src_slot,dst_slot,...)`
  import→catalog→export in-memory; `proof_render` (rs→c smoke symbol);
  boot depends on forge `metal` feature. Gate bios+efi `forge render ok`.
  Next: W13.3 Microdot routes.
- 2026-08-02 — W13.1 DONE: mtar `MAX_FILES` 256→2048; synthetic dir
  prefix open/stat/readdir for file-only packs; reg C ABI
  `mod_at`/`mod_entry_*`/`dyn_at`/`proof_reflect`; rootfs
  `proof_src_browse` (readdir `reg` + read deep source). Gate bios+efi:
  `src browse ok`, `reg reflect ok`, `ready ok`. Host reg smoke reflect ok.
  Dialect/IDE clean on touched paths. Next: W13.2 forge render.
- 2026-08-02 — W12.3+W12.4 DONE / W12 wave DONE: radeon_rv370 + i915_855gm
  C ports (unverified on QEMU) + thin Rust Ops; Kconfig per-backend + UI
  boot stripe; virtio `static_mut_refs` tidy via `addr_of_mut!`. Gate:
  bios `bochs_flip` + `gfx ui stripe ok`; efi `gop_blt` then `bochs_flip`
  + stripe; `mod check` ok.
- 2026-08-02 — W12.2 DONE: `_virtio_gpu` (control+cursorq; contiguous
  ATTACH_BACKING; correct SET_SCANOUT layout) + `_gop` + EFI
  `gop_stash`/`gop_blt_port` (local GOP GUID) + BIOS stubs. Harvest prefers
  Bochs then EFI stash; bochs probe requires `owned=1` so pre-EBS uses
  `gop_blt`. Bringup calls `pm_metal_dev_gfx_efi_pre_ebs` before
  `leave_firmware`. Forge: `METAL_SCANOUT_VIRTIO_GPU=1` adds
  `-device virtio-gpu-pci`. Verified bios `bochs_flip` / virtio_gpu; efi
  `gop_blt` then `bochs_flip`; dialect ok. **Restart clangd** (bringup may
  show stale undeclared `efi_pre_ebs` until reload). Next: W12.3 HW.
- 2026-08-02 — W12.1 DONE + W12.2 partial: Replaced `pm_metal_dev_gfx_detect`
  stub with real harvest→init→present. Private stems `_scanout` (probe order
  bochs then lfb), `_bochs` (VBE virt_h flip + DIRECT caps), `_lfb`
  (chunked memcpy), `_harvest` (PCI 1234:1111 DISPI program 1024x768),
  `_compositor` (shadow, clear/fill/text/present), VGA font bin. Boot IO
  gained `out16`/`in16` (PCI/input IoOps structs updated). Forge run adds
  `-vga std` (display still `none` for CI). Verified bios+efi serial
  `gfx present ok (bochs_flip)` under `ready ok`. Remaining W12.2:
  virtio_gpu + gop_blt; then W12.3 HW; W12.4 UI+Kconfig.
- 2026-08-02 — **Correction:** removed `wasm/_doom.rs` + bringup
  `pm_metal_wasm_proof_doom` (app-specific / test harness in product
  source). External apps stay in sibling repos + `METAL_EXT_APPS` staging
  only; Metal keeps generic wasm load/callin/guest-coro APIs.
- 2026-08-02 — **Correction:** removed `py/_pmcmd.rs` + `pm_metal_py_pmcmd_*`
  + `!module.func` REPL escape per owner (chat: no command registration /
  shell thingy — REPL only). Bringup host→wasm stress now uses
  `pm_metal_reg_call0` directly. Faces resynced.
- 2026-08-02 — W11.7 DONE / W11 wave DONE. Gate: bios+efi QEMU both print
  `wasm stress: ok=768 err=0 reloads=4 ... ok`, tree `concurrency ok`,
  `ready ok`; `CONFIG_PM_METAL_PY_ENABLE_REPL_SHELL=y` still in
  autoconf; `forge-cli mod check` 61 modules ok; py + wasm host smokes
  ok. Main-recheck gaps (log only, no scope expand): (1) Python
  `async`/`await` compile still omitted — concurrency proof uses Rust
  asyncio rewrite; (2) `emitnative`/`emitnx64` still OMIT; (3) no pmcmd
  (owner: REPL only; reg via Python module tree later); (4) runners are
  logical 1/CPU drained cooperatively on UP (no AP threads yet); (5)
  restart clangd after face sync for bringup `proof_stress` IDE diagnostic.
  Next wave: W12 gfx.
- 2026-08-02 — W11.6 DONE: `wasm/_stress.rs` + `pm_metal_wasm_proof_stress`
  on `__init__.rs` (underscore stems are not face-synced). Covers host→guest
  + reg call0, wasm↔wasm (`sample.announcer`→`sample.greeter` via
  `fwd_native`), concurrent async callers, unload/reload under traffic
  with cache invalidate. Bringup also runs host→wasm via
  `pm_metal_reg_call0("tests.wasm_hello","ready")` after
  `wasm_proof` leaves hello packs loaded. **UP quiesce fix** (required for
  unload after `async_start`): `take_ready` returns `INVALID` when parked
  instead of spinning; `reg::_kernel::with_quiesce` drives
  `pm_metal_async_run_poll_all` until `all_parked`. Directions matrix
  (honest): RS↔RS / C↔RS already in reg/bringup; host↔guest + wasm↔wasm
  in this chunk; pure C↔C same-TU not a separate reg cell. Verified: wasm
  host smoke ok; bios `wasm stress: ok=768 err=0 reloads=4 ... ok` +
  `ready ok`; efi build green.
- 2026-08-02 — W11.5 DONE: Engine counters in `async/_impl/_engine.rs`
  (spawn/await/step + max inter-step gap) exposed via new stem
  `async/_impl/metric.rs` (`pm_metal_async_metric_{reset,spawns,awaits,
  steps,starve_max_us}`). Py proof `pm_metal_py_proof_concurrency` in
  `_proof.rs` (await proof moved there too): reset metrics, spawn 32
  concurrent Metal-asyncio `sleep_ms(0)` tasks, `gather_poll` without
  helper sleeps (avoids handle exhaustion), then one nest-await parent
  per runner; requires spawn/await totals + multi-runner step activity
  when `n_runners>1` (work-steal can idle a late runner in a short host
  wave — firmware wave showed equal `r0..r3` steps). UART log line +
  `pm_metal_boot_tree_note_concurrency` / tree `concurrency ok`. Host
  smoke starts `pm_metal_async_start(4)` before B6; B14 calls the proof.
  Verified: `py W4.2 smoke ok`; bios serial
  `py concurrency: runners=4 tasks=32 ... active=4 ... ok` and tree
  `concurrency ok` under `ready ok`; efi build green.
- 2026-08-02 — W11.3 + W11.4 DONE: UART RX — extended
  `pm_metal_boot_uart_ops_t` (`boot/platform/uart.h`) with non-blocking
  `try_getc` (-1 empty, else 0..255), implemented in bios+efi `uart.c`
  (COM1 LSR bit 0x01 data-ready check, same `pm_metal_bios_inb`/
  `pm_metal_boot_inb` port helpers each file already used — no new
  inline surface added, per the existing-inline grandfather note); new
  `pm_metal_dev_serial_try_read(buf, cap)` in `dev/serial/_impl/
  __init__.rs` calls through the ops-table `try_getc` fn pointer (same
  ops-vtable pattern the existing `write` path already used). REPL boot
  shell: `py/_shell.rs` is one `pm_metal_async_spawn` task (`start()`/
  `running()`, `AtomicBool` guard against double-start) that on first
  step resets the loop and writes `>>> ` directly to serial (bypassing
  the line-buffered mphal stdout so the prompt is never held back by a
  missing trailing `\n`), then each step drains up to a bounded chunk of
  serial bytes, echoes with `\r`->`\n` folding, feeds
  `pm_metal_py_loop_feed`, calls `pm_metal_py_loop_step`, and prints
  `... ` on entering continuation / a fresh `>>> ` on return to idle —
  never blocks (satisfies `metal-no-long-running-ops`). C ABI
  `pm_metal_py_shell_start`/`pm_metal_py_shell_running` bound onto `reg`
  in `_bind.rs`; called once from `bringup.c` right after the boot-tree
  print (forge's `#pm-metal/boot-tree/ready` needle) and before
  `pm_metal_async_run_loop()`, gated behind a real
  `#ifdef CONFIG_PM_METAL_PY_ENABLE_REPL_SHELL` (not a no-op toggle).
  **No pmcmd** (owner directive: no command-registration / shell
  thingy — REPL is the only interactive surface; reg integrates into
  the Python module system later). `py/.pm/Kconfig` (W11.4) adds
  `CONFIG_PM_METAL_PY_ENABLE_REPL_SHELL` (bool, default `y`) plus three
  `comment` entries documenting *why* heap sizing, REPL buffer constants,
  and `DYNAMIC_COMPILER` are deliberately not Kconfig-driven yet (py has
  no private arena — always the one shared `mem` heap; REPL buffers are
  compile-time Rust consts no Rust module reads `autoconf.h` for yet;
  lexer/parse/compile stay unconditionally linked, no frozen-bytecode-only
  cutdown build exists to gate them out of) — sourced from
  `config/metal/Kconfig` next to `fs`/`boot/rootfs`'s existing
  `.pm/Kconfig` entries, `defconfig`/`.config` both updated. Host smoke
  extended with B13: `pm_metal_py_shell_running()==0` on host (no UART).
  `py W4.2 smoke ok` holds; `forge-cli mod sync` + `mod check` green;
  `forge-cli build bios`/`build efi` both green; interactively exercised
  over the bios QEMU serial unix socket — typed a Python expression
  (auto-printed, fresh `>>> `) with correct prompt/echo behavior.
- 2026-08-02 — W11.2 DONE: `py/_loop.rs` cooperative REPL loop (line
  buffer + persistent globals dict + internal feed ring, spinlock-guarded
  session state matching `frozenmod.rs`'s established pattern) +
  `pm_metal_py_loop_step`/`feed`/`reset`/`last_result_i32`/
  `last_result_valid` C ABI, bound onto `reg` (`_bind.rs`) and synced into
  every generated face (`forge-cli mod sync`). `py/port/mphalport.c` real
  I/O: stdout line-buffers into `pm_metal_log` (generated `log/__init__.h`
  — log is `impl=rs`, non-spine, so the ABI-hub rule wants the generated
  header, not a hand weak-declare like the old stub had); stdin reads one
  byte from the attached Metal stdio stream via `dev.stream`'s own human
  header (`impl=c`, same-language direct link — matches `net/ssh`'s
  already-established `#include <pymergetic/metal/dev/stream/__init__.h>`
  pattern, no registry indirection needed); ticks use the real calibrated
  `pm_metal_time_mono_us` (`async/time.h` — searched for an existing
  clock before adding one, found it); `delay_us` stays a documented
  no-op (HAL must not block, see `metal-no-long-running-ops`). New
  `py/.pm/build.rs` (`cc` crate, mirrors `mem`/`net.ip`'s build.rs shape)
  compiles `mphalport.c` into the `pymergetic_metal_py` staticlib for
  every target; the host smoke binary (no forge C-unit link step of its
  own) also bundles `dev/stream/__init__.c` there, while firmware
  (bios/efi) skips that second copy since forge's own `PRODUCT_COMMON`
  table already supplies it at the final `ld` — verified with `nm` that
  every `pm_metal_stream_*` symbol appears exactly once in `metal.elf`.
  Added `src/pymergetic/metal/py/port/.clangd` (mphalport.c is
  cargo-compiled, not a forge Unit, so it has no CDB row and needs its
  own Generic-fallback `-I`s — mirrors `wasm/port/.clangd`'s shape).
  Smoke B12: loop reset/feed/step round-trip for `1+2` (auto-print +
  the two-part `last_result_i32`/`last_result_valid` seam), `def f():`
  NeedMore continuation, `pass` statement (last-value seam honestly
  clears, no stale int), and a real bounded line-overflow `-1`. `py
  W4.2 smoke ok` holds; `forge mod check` 61/61 ok; bios + efi both
  build and link clean (checked with `nm`, no duplicate symbols).
- 2026-08-02 — W11.1 DONE: `upy/py/frozenmod.rs` (fixed-capacity
  spinlock-protected `name -> 'static source bytes` table;
  `register_str`/`find`/`load_as_source`; frozen-`.mpy` kind omitted —
  no loader exists yet, string-only is the finished path) and
  `upy/py/repl.rs` (`continue_with_input` faithful port of
  `mp_repl_continue_with_input`, adapted for this mirror's
  always-`\n`-terminated line convention; `exec_line` thin lex/parse/
  compile/vm glue with `ReplMode::{Single,File}` and a caller-owned
  persistent `globals` dict for real multi-line REPL sessions). Wired
  into `upy/py/mod.rs`; `reader.rs` grew a `Reader::borrow` (non-`'static`
  mem reader) that `new_mem` now reuses. Smoke B11 green: register/find/
  duplicate-rejection, `def f():` needs-more vs `pass` complete,
  frozenmod payload run + read back through a shared globals dict,
  stateless `1+2` eval and `pass` statement, `def f():` `NeedMore`.
  `emitnative.c`/`emitnx64.c` intentionally left unmirrored (3122+ LOC;
  bytecode VM is the Metal execution path for W11 REPL) — see
  `ORCHESTRATION_UPY_MIRROR.md` rows. Full `py W4.2 smoke ok` holds.
- 2026-08-02 — W10.1 DONE: forge always-proxy for non-spine Rust faces;
  `resolve_import` + floor `*_mod_load` RegMods (console/log/ascii +
  detectors); boot `_bootstrap` loads them before harvest/banner.
  Spine fast-path kept: `pymergetic.metal`, `mem*`, `reg*`, `async*`
  (async earned: reg path-includes quiesce face). Face check: scrape
  plain `pub unsafe fn` as non-inline. Gate: mod check clean; bios+efi
  build; bios QEMU banner OK. Main recheck: deferred to W10.3.
- 2026-07-31 — W9 tidy: loopback GET /health proof OK bios+efi
  (`httpd: health ok`). Root cause: server `sync_tcp_write` nested
  `run_poll(_all)` inside listen handler; switched to `try_write` +
  `net_ip_poll` only.
- 2026-07-31 — W9.1–W9.4 DONE: `http/server` conn_method/target/hdr/send +
  autoload (:80, /health + Microdot `/`); `http/microdot` register/get/route/
  handle; bind_reg into publish_kernel; bios+efi `httpd: listening :80` +
  prior proofs. Omitted (no hollow): Py/Wasm register, WS, JSON httpd.json,
  loopback GET proof (attempted; client->127.0.0.1 not green yet).
- 2026-07-31 — W9 started (inventory): live `http/server.rs` already has
  C-mount ASGI (listen/mount/send_simple/health). Steal from `_old` asgi:
  Py/Wasm register, WS, config/autoload, shell. Next W9.1 fold those into
  `server` (no revive `net/asgi/`); then W9.2 `http/microdot/`.
- 2026-07-31 — W8.3+W8.4 DONE: `net/ssh` impl=rs + build.rs (Dropbear +
  glue + CRT); listen/autoload/bind_reg; bringup autoload after DHCP;
  bios+efi serial `sshd: listening :22 (dropbear)` + prior proofs. Shell
  viewport mirror omitted (no console mirror yet). No seeded password
  hash; bcrypt still deferred.
- 2026-07-31 — W8.3 (partial): finished `auth` Argon2id + SSH pubkey table
  (monocypher in COMMON); faces sync; bios+efi build; serial proofs hold.
  No bcrypt yet (crypt_blowfish vendor blocked by auto-review). No
  sslcert/fs reload (live fs is async-only). Still TODO: Dropbear
  glue/`build.rs`, tcp adapt, bind_reg, bringup autoload.
- 2026-07-31 — W8.3 (partial): finished `dev/stream` pipe/pty + async
  read/drain (ISO C); forge COMMON `stream.o`; faces sync; bios serial
  still `ok` / `await ok` / `wasm pack ok`. Still TODO in W8.3: `auth`
  port, Dropbear glue + `build.rs`, tcp sock->stream_h adapt, bind_reg,
  bringup autoload.
- 2026-07-31 — W8.2 DONE: restored 37 `dropbear_stubs/` + 4
  `dropbear_metal/` headers from `_old`; `patches/dropbear/0001-...`;
  `scripts/setup.d/deps/dropbear.sh` (ROOT=metal package); vendored
  `external/dropbear` @ DROPBEAR_2024.85 with Metal patch applied
  (idempotent re-run). Not yet a git submodule entry.
- 2026-07-31 — W8.1 DONE (inventory, no hollow copy). Live `net/ssh/` is
  `.gitkeep` only. Steal from `_old/.../net/ssh/`: stubs (~35 headers),
  `dropbear_metal/{config,localoptions,default_options_guard,io}.h`,
  glue `dropbear_{fd,posix}.c` + `ssh_{dropbear,server,config,shell}.c`
  (~3k LOC), patch `0001-metal-session-auth-pty.patch`, setup pin
  `DROPBEAR_2024.85`. Live deps to adapt: TCP is
  `pm_metal_net_ip_tcp_*` (not old sock_h); `dev/stream` + `auth` missing
  in live (only `_old` finished bodies) — restore/port those before
  listen/shell proof, do not stub. Vendor: `external/dropbear` +
  `patches/dropbear/`. Border: `pm_metal_net_ssh_{listen,close,autoload,status}`
  + `*_bind_reg` full names; async listen coro (old pattern).
- 2026-07-31 — W7.2 / W7.1 DONE: `pm_metal_reg_publish_kernel` (firmware
  calls py/console/log `*_bind_reg`); `register_rows_bytes` helper; bringup
  switched; host smoke bulk-register + host publish_kernel=-1; faces sync;
  bios+efi serial still `ok` / `await ok` / `wasm pack ok`. Perf: none noted
  (same proof path).
- 2026-07-31 — W6.3 / W6.2 / W6.1 DONE: `tests/wasm_hello` (rs) +
  `tests/wasm_hello_c` (c); `forge pack` (cargo wasm32 / clang+wasm-ld);
  forge build packs before cargo boot; proof loads both -> `wasm pack ok`
  on bios+efi.
- 2026-07-31 — W5.3 DONE: freestanding WAMR in firmware; serial `wasm ok`.
- 2026-07-31 — W5.2 / W5.1 / W4 / W3 / W2 / W1 / W0 DONE.
- 2026-07-31 — Standing: **async-first OS** (Locked #5).
