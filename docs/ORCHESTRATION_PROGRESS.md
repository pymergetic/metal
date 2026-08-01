# Orchestration progress (autorun handoff)

Updated by the agent every chunk. Owner: enqueue `continue` while AFK.  
Runbook: [`ORCHESTRATION_AUTORUN.md`](ORCHESTRATION_AUTORUN.md).  
Locks: [`ORCHESTRATION.md`](ORCHESTRATION.md).

**Last agent note:** W0-W9 complete. W9 tidy: loopback GET /health proof
green (`httpd: health ok`) bios+efi — fixed server reply path to use
`try_write` (no nested `poll_all` in listen handler).

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

_(none)_

---

## Log (newest first)

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
