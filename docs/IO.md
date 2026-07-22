# Metal IO classes + device table

Companion to [`LIBC_ASYNC.md`](LIBC_ASYNC.md) and [`COOP_MEMORY.md`](COOP_MEMORY.md).  
**Product IO is Metal ABI + host backends**, not WASI.

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
| **gfx** | size/origin | clear/fill/blit + sync `present` | `async_present` (eager fence today) | software+GOP, GL later |
| **audio** | `ready` | `queue` | `drain` | virtio-snd (probe), else null |
| **input** | `poll_key`, `poll_pointer` | — | omit v1 | efi ConIn+i8042 / bios i8042 |
| **fs** | transitional sync size/read | — | open/close/read/write + sync `lseek`; stat/readdir/mkdir/unlink/rename/fsync | ESP cache (+ SimpleFileSystem pre-EBS) |
| **blk** | `count`/`at`/`ready`/`capacity` | — | `read_async`/`write_async` | virtio-blk, ide-ata (multi-device) |
| **stream** | attrs/winsize | `write` if space | `read` / drain | uart, ui_tab, pipe, pty, virtio-console |
| **net** | — | `send` if space | connect/listen/accept/recv/dns | lwIP + virtio-net L2 (probe), else null |
| **random** | `random` fill | — | — | EFI RNG / weak |

Rules: guest never imports EFI device protocols as product ABI; backend vtable + DT node per live class.

---

## Metal DT (device / capability table)

Append-only inventory of **present** devices: class + `compat` + caps + bus/loc.  
Not Linux FDT. Multiple nodes per class are normal. Platform-agnostic — QEMU and real PCs share the same table shape; only discovered nodes differ.

```text
time/tsc
gfx/framebuffer
fs/esp | fs/embed
input/ps2+com1
stream/uart+ui
stream/virtio-console#1     (if detected)
net/lwip+virtio-net | net/null
audio/virtio-snd | audio/null
random/efi-or-weak | rdrand-or-weak
blk/virtio-blk#0            (if detected)
blk/ide-ata#0               (if detected; master/slave each get a node)
```

**Detect → add → bind:** harvest runs every linked detector. Each detector adds zero or more DT nodes for what it finds and binds its driver — no fallback chain (virtio and IDE both run; both can appear).

Blk detectors: `pm_metal_blk_virtio_detect`, `pm_metal_blk_ide_detect` (legacy ISA `0x1F0`/`0x170` IDENTIFY). Virtio-pci IDs unchanged: net `0x1041|0x1000`, blk `0x1042|0x1001`, console `0x1043|0x1003`, sound `0x1059`.

**Harvest vs product bind:** sync floor harvests DT + GOP/framebuffer; boot tree prints full DT inventory under `devices`. After EBS / BIOS floor the seeded init task binds gfx/UI/wasm/shell. Proofs are manual (`test` shell command). **UI is not a DT node.**

API: `pm_metal_io_dt_add` / `get` / `count` / `by_class` / `foreach` / `lookup` (first of class). Guest `io_query` optional later.  
Tree: `bus/` (DT + virtio PCI) · `dev/<class>/` (detectors + backends).

---

## Stream plumbing (stdio / TTY / pipes)

Endpoints: `uart`, `ui_tab`, `pipe`, `pty` (master/slave), later `virtio_console`.  
`stdio_attach(in,out,err)` binds a session. Dropbear later: net ↔ pty master; slave as remote TTY.

Cooked termios / job control: omit until needed (raw PTY first).

---

## Input notes

- Product keycodes are **Metal** HID usage IDs (positional USB usage; not KEYB layout).
- Shell ASCII uses DOS KEYB (`keyb us` / `keyb gr`); guests keep HID under layout changes.
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
Next: guest apps on tab surfaces when ready.
