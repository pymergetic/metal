# Metal IO classes + device table

Companion to [`LIBC_ASYNC.md`](LIBC_ASYNC.md) and [`COOP_MEMORY.md`](COOP_MEMORY.md).  
**Product IO is Metal ABI + host backends**, not WASI and not doom-special.

---

## How we build

| Kind | Rule |
|------|------|
| **sync** | Bounded CPU on the current runner, or non-blocking ring put |
| **sync façade** | Format/copy then enqueue; never park the worker |
| **async** | Returns a handle; completion only via `await` / `guest_step` |
| **omit** | No threads, signals, `select`/`poll`, `setjmp` across await |

Wait on the world → **async**. CPU work → **sync**. Same classes on host and guest.

### Python-shaped async

| Python | Metal |
|--------|--------|
| awaitable / `async def` | `pm_metal_coro_t` / `pm_metal_guest_step` |
| `await x` | `pm_metal_await` / `pm_metal_async_await` |
| `create_task` | `pm_metal_create_task` / `pm_metal_async_create_task` |
| `sleep` / deadline | `sleep_us` / `sleep_until_us` |
| `sleep(0)` | `yield` |
| event loop | **N equal runners** (`run_poll` / `run_loop`) |

### N CPUs = N runners — no Extrawurst

- `n_cpus` stacks + inboxes + cooperative runners — all equal.
- Same work/input path; **FCFS** — who drains a ready task first serves it.
- **No CPU0 pin** for wasm/guest sessions. `create_task` round-robins; session pump drains **all** inboxes.
- Never await while holding a lock.

---

## IO class table

| Class | Sync poll / get | Sync façade | Async await | Backends (examples) |
|-------|-----------------|-------------|-------------|---------------------|
| **time** | `mono_us`, wall clock | — | `sleep_us`, `sleep_until_us` | TSC |
| **gfx** | size/origin | clear/fill/blit | `present` | software+GOP, GL later |
| **audio** | `ready` | `queue` | `drain` | virtio-snd (probe), else null |
| **input** | `poll_key`, `poll_pointer` | — | omit v1 | efi ConIn+pointer |
| **fs** | `lseek` | — | size/read/write | ESP (pre-EBS), virtio-blk raw |
| **stream** | attrs/winsize | `write` if space | `read` / drain | uart, ui_tab, pipe, pty, virtio-console |
| **net** | — | `send` if space | connect/recv/accept/dns | lwIP + virtio-net L2 (probe), else null |
| **random** | `random` fill | — | — | EFI RNG / weak |

Rules: guest never imports EFI device protocols as product ABI; backend vtable + DT node per live class.

---

## Metal DT (device / capability table)

Host registry at boot: class → backend name → caps.  
Not Linux FDT required in v1.

```text
time/tsc
gfx/software
audio/virtio-snd    (else audio/null)
input/efi
fs/esp              (pre-EBS); fs/virtio-blk when probed
stream/ui_tab+pipe+uart / virtio-console
net/lwip+virtio-net (else net/null); static IPv4 (QEMU SLIRP defaults); shell `net` / `metal/net.conf`
random/efi-or-weak
```

Probe order: virtio-pci (`1AF4`) via `EFI_PCI_IO` then BAR→MMIO map for post-EBS: net `0x1041|0x1000`, blk `0x1042|0x1001`, console `0x1043|0x1003`, sound `0x1059`. QEMU: virtio-net/snd + `-device virtio-blk-pci` + `-device virtio-serial-pci,max_ports=1 -device virtconsole`.

**Harvest vs bind:** sync floor harvests DT/virtio + GOP; boot tree prints `mem` / `cpu` / `devices`. After EBS the seeded init task binds gfx/UI/wasm/shell. Proofs are manual (`test` shell command). **UI is not a DT node.**

API: `pm_metal_io_dt_register` / `pm_metal_io_dt_lookup` (host). Guest `io_query` optional later.

---

## Stream plumbing (stdio / TTY / pipes)

Endpoints: `uart`, `ui_tab`, `pipe`, `pty` (master/slave), later `virtio_console`.  
`stdio_attach(in,out,err)` binds a session. Dropbear later: net ↔ pty master; slave as remote TTY.

Cooked termios / job control: omit until needed (raw PTY first).

---

## Input notes

- Product keycodes are **Metal** codes (not doom scancodes).
- Pointer lock is surface-scoped (`DEFAULT` or tab); unlock on Escape / lifecycle blur.
- UI cursor + hit-test when unlocked (separate from lock).

## Lifecycle

- Sync poll: `pm_metal_lifecycle_poll` / `lifecycle_focused`.
- Host sets focus on session start; `lifecycle_blur` on session end (unlocks pointer).

---

## Status

Landed: DT, time µs, present, audio null, input pointer/lock, FS write, lifecycle,
stream (uart/ui_tab/pipe/pty + stdio_attach), net lwIP (NO_SYS) + virtio L2, random/realtime,
tab surfaces + clipped present, unlocked UI cursor + tab-strip hit-test,
`shell_log`/WASI stdout → stdio streams, `gfx_set_surface` for tab-clipped guest draw.  
Next: unpark doom on tab surface when ready. Doom stays parked.
