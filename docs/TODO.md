# Metal TODO — honest leftovers

Not a dump of wishes. Only gaps that still smell after the 2026-08-09
incompleteness pass.

## Muscle

| Item | Status |
|------|--------|
| WAMR host natives (log/async/mem/fs + WASI stubs) | Landed — process/audio/gfx/input/shell deferred |
| Async quiesce ↔ C runner park | Landed — mid-step cancel still out of scope |
| SSH post-NEWKEYS encrypt + password auth | Landed — **PTY→Metal console** + `client_exec` later |
| `upy_io_fill.c` HTTP park fill | Landed — wire `mp_wasm_io_set` when full wasmmod fetch is on FW |
| Browser HAL fail-closed (`port/hal/wasm/*_stub.c`) | Intentional seat honesty — not debt |

## Docs / seats

| Item | Status |
|------|--------|
| i686 empty `ap_trampoline32.S` | SMP bring-up incomplete on i686 |
| `ENGINE=upy` path | Advertised; under-proven vs default WAMR smoke |
| OpenSSH live auth proof (`LIVE_SSH`) | Prefer over trusting smoke listen-only |
| Guest tests needing audio/input/gfx hubs | Stay unresolved until those faces exist |

## Iron / smoke

Keep FW greps requiring `ssh ok` / `ssh py ok` (no `ssh stub` escape).
Browser remains fail-closed for nested WAMR / PCI / NIC.
