# Orchestration progress (autorun handoff)

Updated by the agent every chunk. Owner: enqueue `continue` while AFK.  
Runbook: [`ORCHESTRATION_AUTORUN.md`](ORCHESTRATION_AUTORUN.md).  
Locks: [`ORCHESTRATION.md`](ORCHESTRATION.md).

**Last agent note:** _(none yet — prep only)_

---

## Wave status

| Wave | Status | Notes |
|------|--------|-------|
| W0 Prep | `TODO` | mod check + bios/efi sanity; Progress live |
| W1 reg/ | `TODO` | |
| W2 Vendor + py edge | `TODO` | needs network for micropython |
| W3 Inventory B0–B7 | `TODO` | see bands below |
| W4 Py proofs | `TODO` | bios+efi |
| W5 wasm host | `TODO` | |
| W6 packs C+rs | `TODO` | tests/ |
| W7 wire | `TODO` | |
| W8 Dropbear SSH | `TODO` | after W0–W7; dirs `src/.../net/ssh/` already exist |
| W9 http complete | `TODO` | after W8; finish ASGI in `http/server` + `http/microdot` runner |

Status values: `TODO` | `IN_PROGRESS` | `DONE` | `BLOCKED`

**Per continue:** (1) tidy-check last DONE → (2) chunk → (3) gate →
(4) **full lints** → (5) update this file.  
**Always:** async-first OS (ORCHESTRATION Locked #5).

---

## Chunk queue (first unfinished wins)

- [ ] **W0.1** `./forge-cli mod sync && ./forge-cli mod check` green  
- [ ] **W0.2** `./forge-cli build bios` + `./forge-cli build efi` green  
- [ ] **W1.1** Create `src/pymergetic/metal/reg/` `.pm/module` + Cargo + `__init__.rs` border  
- [ ] **W1.2** `table.rs` + `bind.rs` (full module names; flat map)  
- [ ] **W1.3** `.pm/smoke.rs` register/lookup/bind  
- [ ] **W1.4** `mod sync` + `mod check`; bios+efi still build  
- [ ] **W2.1** Vendor `external/micropython`  
- [ ] **W2.2** Create `py/` Metal edge modules (loop/alloc/gc_off/async_bridge/bind/port) — finished slices only  
- [ ] **W2.3** Gate: sync/check/bios/efi  
- [ ] **W3.B0** Inventory band B0 (mpconfig/misc/mpstate/qstr/obj faces)  
- [ ] **W3.B1** Band B1 (malloc→Metal, gc dead, runtime/vm/bc)  
- [ ] **W3.B2** Band B2 (essential objects)  
- [ ] **W3.B3** Band B3 (core builtins + import→reg)  
- [ ] **W3.B4** Band B4 (rest objects/builtins)  
- [ ] **W3.B5** Band B5 (extmod keep-list)  
- [ ] **W3.B6** Band B6 (asyncio REWRITE → Metal async)  
- [ ] **W3.B7** Band B7 (shared as needed)  
- [ ] **W4.1** `tests/py_smoke` print proof bios+efi  
- [ ] **W4.2** await/park proof bios+efi  
- [ ] **W5.1** Vendor wamr (or chosen runtime)  
- [ ] **W5.2** `wasm/` host load→reg; one memory  
- [ ] **W5.3** Gate bios+efi  
- [ ] **W6.1** `tests/wasm_hello` impl=rs pack  
- [ ] **W6.2** `tests/wasm_hello_c` impl=c pack  
- [ ] **W6.3** Forge pack both; load/call bios+efi  
- [ ] **W7.1** Kernel auto-register into reg  
- [ ] **W7.2** Final mod check + bios+efi + note perf if any  
- [ ] **W8.1** Inventory `_old` ssh/dropbear → live `net/ssh/` plan (no hollow copy)  
- [ ] **W8.2** Restore `dropbear_stubs/` + `dropbear_metal/` from `_old` (finished port)  
- [ ] **W8.3** Wire Metal ssh border + async/net; `reg` full names  
- [ ] **W8.4** bios+efi build + listen/proof (`timeout`); full lints  
- [ ] **W9.1** Finish `net/http/server` ASGI (fold needed `_old` asgi: Py/Wasm/WS/config)  
- [ ] **W9.2** Add `net/http/microdot/` runner (`impl=rs` App/route + leaf `handle`)  
- [ ] **W9.3** `mod sync` faces + `reg` full names; smoke  
- [ ] **W9.4** bios+efi mount + GET/reply proof; full lints  

---

## Inventory tally (W3)

| Band | MIRROR/REWRITE done | Notes |
|------|---------------------|-------|
| B0 | 0 | |
| B1 | 0 | |
| B2 | 0 | |
| B3 | 0 | |
| B4 | 0 | |
| B5 | 0 | |
| B6 | 0 | |
| B7 | 0 | |

---

## Blockers

_(none)_

---

## Log (newest first)

- 2026-07-31 — Standing: **async-first OS** (Locked #5) on every plan/API.
- 2026-07-31 — W9 layout: Microdot under `net/http/` with client+ASGI
  (`server`); no sibling `net/asgi/`. Runner role unchanged.
- 2026-07-31 — Correct W9: Microdot = ASGI runner on `net/asgi`, not
  `net/http` client/stack (see IO.md).
- 2026-07-31 — Prep+: W9 Microdot after W8 — Rust rewrite, lang-pool/`reg`
  (all langs); not zip-only guest.
- 2026-07-31 — Prep+: each continue re-verifies last chunk is complete/tidy
  before starting the next TODO.
- 2026-07-31 — Prep+: full lints every continue; W8 Dropbear after W0–W7
  (`net/ssh/dropbear_*` dirs already present).
- 2026-07-31 — Prep: AUTORUN + PROGRESS created; no implementation wave started.
