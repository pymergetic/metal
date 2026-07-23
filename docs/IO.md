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
| **gfx** | size/origin | clear/fill/blit + sync `present` | `async_present` (fence) | compositor shadow → **scanout backend** (`virtio_gpu` / `bochs_flip` / `radeon_rv370` / `i915_855gm` sample / `gop_blt` / `lfb_copy`); harvest: Multiboot / Bochs / VESA / EFI GOP |
| **audio** | `ready` | `queue` | `drain` | virtio-snd (probe), else **AC97** (ICH / QEMU `-device AC97`), else null |
| **input** | `poll_key` / `poll_key_event`, `poll_pointer` | — | omit v1 | virtio-tablet (absolute, QEMU/VNC) when present; else efi ConIn+i8042 / bios i8042; tab focus gates guest vs shell |
| **fs** | transitional sync size/read | — | open/close/read/write + sync `lseek`; stat/readdir/mkdir/unlink/rename/fsync | ESP cache (+ SimpleFileSystem pre-EBS) |
| **blk** | `count`/`at`/`ready`/`capacity` | — | `read_async`/`write_async` | virtio-blk, ide-ata (multi-device) |
| **stream** | attrs/winsize | `write` if space | `read` / drain | uart, ui_tab, pipe, pty, virtio-console |
| **net** | — | `send` if space; `bind_if` | connect/listen/accept/recv/dns (+ ping/http/tftp/ntp modules) | lwIP + `lo` + virtio-net / bge (`eth0`…) |
| **random** | `random` fill | — | — | EFI RNG / weak |

Rules: guest never imports EFI device protocols as product ABI; backend vtable + DT node per live class.

---

## Metal DT (device / capability table)

Append-only inventory of **present** devices: class + `compat` + caps + bus/loc.  
Not Linux FDT. Multiple nodes per class are normal. Platform-agnostic — QEMU and real PCs share the same table shape; only discovered nodes differ.

```text
time/tsc
gfx/framebuffer                 (floor: + `pci VVVV:DDDD @bus:dev.fn` when VGA found; after bind → gfx/<scanout>)
fs/esp | fs/embed
input/ps2+com1
input/virtio-tablet            (if `-device virtio-tablet-pci`)
stream/uart+ui
stream/virtio-console#1     (if detected)
net/loopback + net/lwip+virtio-net | net/lwip+bge   (`lo` + eth0…)
audio/virtio-snd | audio/ac97 | audio/null
random/efi-or-weak | rdrand-or-weak
blk/virtio-blk#0            (if detected)
blk/ide-ata#0               (if detected; master/slave each get a node)
```

**Detect → add → bind:** harvest runs every linked detector. Each detector adds zero or more DT nodes for what it finds and binds its driver — no fallback chain (virtio and IDE both run; both can appear). Floor + bus harvest are common (`boot_harvest.c`); seed/init / proofs / shutdown are common `boot_init.c`. Bind hooks are only the deltas: `pm_metal_boot_port_floor()` (fs/random compat, BIOS PS/2 prep) and `pm_metal_boot_port_seed()` (handoff/`ebs` log).

Blk detectors: `pm_metal_blk_virtio_detect`, `pm_metal_blk_ide_detect` (legacy ISA `0x1F0`/`0x170` IDENTIFY). Virtio-pci IDs unchanged: net `0x1041|0x1000`, blk `0x1042|0x1001`, console `0x1043|0x1003`, input `0x1052`, sound `0x1059`.

**Harvest vs product bind:** sync floor harvests DT + GOP/framebuffer; boot tree prints full DT inventory under `devices`. After EBS / BIOS floor the seeded init task binds gfx → UI → net → wasm → shell (UI paints before NIC open so the FB is not stuck on GOP residue). Proofs are manual (`test` shell command — registered from `boot_shell.c`). **UI is not a DT node.**

### Net (multi-if + DHCPv6)

- Host ifs: always `lo` (127.0.0.1/8, ::1) plus `eth0`… (`PM_METAL_NET_MAX_IFS`). Default route prefers ethN when present. Shell: `net status [lo|ethN]`, `net set [ethN] …`, `net set [ethN] dhcp`, `net set [ethN] dhcp6 off|stateless|stateful`.
- DHCPv6: **stateless** via lwIP; **stateful** via Metal client (`metal_dhcp6_stateful_*`) — lwIP `dhcp6_enable_stateful()` remains a stub.
- Guest sockets: `pm_metal_net_bind_if(h, "lo"|"eth0")` before connect/listen (NULL → default).
- **Name layers:** `util/ip` = IPv4 literals only (`ip4_parse` / `ip4_is_literal`). Local nodename = sync `pm_metal_host_name_get/set` (default `metal`; shell `hostname`; optional `hostname=` in `metal/net.conf`; sent as DHCPv4 option 12). Resolve order for connect/dns: literal → `localhost`/nodename → VFS `/etc/hosts` (ESP `etc/hosts`) → async DNS. After successful `pm_metal_net_dns` await: `pm_metal_net_dns_last_ntoa` (guest/host). Shell: `nslookup <host>`.
- **DHCP boot/TFTP:** lease exposes next-server (`siaddr` / opt 66) + boot file (BOOTP file / opt 67) via `pm_metal_net_if_boot_get` / `ifcfg.tftp`+`boot_file`. Generic async client: `pm_metal_net_tftp_get(host, path, dest, cap)` (host/path empty → DHCP next-server + bootfile). Guest proof: `async_tftp` (EFI verify uses QEMU `-netdev user,tftp=…,bootfile=…`).
- **Net life:** background coro (`pm_metal_net_life_start`) — fast poll while no lease, slow while up; HTTP doom seed (~2s after lease, before NTP; host = DHCP next-server, else gw, else `192.168.10.1`:8080); NTP after. DHCP/NTP success quiet; HTTP pkg fail logs status+URL. `run doom` waits ~30s for IWAD seed if wasm was already on ESP.
- **Tray colors:** red = no IPv4; amber = IPv4 but no DNS string; green = IPv4 + DNS (slot 0 or backup slot 1).
- **Fullscreen guest (`run`):** shell skips chrome/prompt paint and status dirty; host pump sleeps 1 ms while a process is live. `blit_bgra` writes the logical surface; one `async_present` fence per guest frame. Apps never select a scanout backend.
- **Scanout backends** (`include/.../gfx/scanout.h`, probe at gfx bind): compositor stays in `gfx.c`; present goes through `pm_metal_scanout_ops`.
  1. **`virtio_gpu`** — virtio-gpu resource + `TRANSFER_TO_HOST_2D` / `RESOURCE_FLUSH` (QEMU: `METAL_SCANOUT_VIRTIO_GPU=1 ./scripts/run efi`)
  2. **`bochs_flip`** — QEMU stdvga VBE virt_h 2× + `Y_OFFSET` page-flip (+ optional DIRECT shadow in back page)
  3. **`radeon_rv370`** — T43 Mobility X300 (`1002:5460`): PCIe GART→shadow, MMIO 2D→front (no flip); probe requires VRAM blit + GART readback or fall through to `lfb_copy`
  4. **`i915_855gm`** — sample Gen2 Intel (PCI `8086:3582` T42): GGTT + ring blit + `DSPAADDR` flip
  5. **`gop_blt`** — pre-EBS EFI GOP `Blt`
  6. **`lfb_copy`** — post-owned chunked shadow→LFB memcpy (generic iron fallback)
  Layers: **lower** (`scanout_*`) = physical copy/flip only (no busy-wait); **upper** = surfaces for widgets; **ops** = blit/fill/text/present on surfaces. Shared **60 Hz** frame pace. Caps: `TEAR_FREE`, `CHUNKED`, `DIRECT` (not auto). Bound backend shows on init (`| +-- gfx ok <scanout> WxH`) and in the DT compat after bind (`hwinfo`).
- **SNTP:** `pm_metal_net_ntp_sync(host)` (UDP/123); on success sets wall via `pm_metal_realtime_set_unix_ms`. Status clock / `date` use local TOD (`tz` minutes; default `Europe/Berlin` +120; `metal/net.conf` `tz=` / `timezone=`). Clock tint amber until NTP; tray ifaces green = IP+DNS (lo: IP only), amber = IP without DNS, red = down.
- **Ping / DNS under QEMU user-net:** SLIRP may drop or ignore ICMP; `ping` retries briefly for ARP/`ERR_RTE`. Shell jobs must treat `WAITING` like `PENDING` (sleep/DNS park). `net` shows DHCP DNS (QEMU → `10.0.2.3`); `8.8.8.8` is only a silent lwIP backup if that proxy fails. Prove L3 with `nslookup`, HTTP, or TFTP when ICMP is filtered.

### Shell command registry

Topic modules place commands with `PM_METAL_SHELL_CMD` / `PM_METAL_SHELL_CMDS` into linker section `.pm_metal_shell_cmds.*`. `pm_metal_shell_cmds_install()` walks `__pm_metal_shell_cmds_{start,end}` (cap `PM_METAL_SHELL_CMD_MAX` = 128). No manual `register_*` list.

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
- Guest ABI: `pm_metal_input_poll_key_event` (preferred) and convenience `pm_metal_input_poll_key` (pressed + code). Host and wasm imports share the same shapes.
- Tab focus: shell vs guest; only the focused surface drains keys (avoids shell eating guest input).
- Shell ASCII uses DOS KEYB (`keyb us` / `keyb gr`); guests keep HID under layout changes.
- Pointer lock is surface-scoped (`DEFAULT` or tab); unlock on Escape / lifecycle blur.
- UI software cursor (save/restore + dirty-rect present) + tab-strip hit-test/hover when unlocked.
- **Console scrollback:** each tab console holds a 1024×160 ring (`CONSOLE_BYTES_MAX`); `view_off` sticks to bottom unless scrolled. Scrollbar on the right; shell focus handles wheel (`PTR_WHEEL`, `dy` = ticks toward older history), thumb click-drag, track jump, and PageUp/PageDown (`pm_metal_ui_console_scroll_page`).
- **Command history:** shell keeps up to `PM_METAL_SHELL_HISTORY_MAX` (64) committed lines (skips empty / consecutive dup). Up/Down recalls like bash (`PM_METAL_KEY_*` or CSI `ESC [ A/B` on the ASCII path); `history` lists them.
- **Prompt:** bash-like `hostname:~$ ` (green host / blue `:~` / green `$` on UI + COM1 ANSI; space after SGR reset so serial keeps it).
- **Tabs:** click the tab strip, shell `use <n>`, or **Ctrl+Shift+Left/Right** (wraps; works under shell or guest focus so a guest tab is not a dead end). Needs i8042 make/break (BIOS / EFI post-owned); plain ConIn has no modifier state.
- **Pointer:** Pre-EBS EFI uses Absolute/SimplePointer (wheel via `RelativeMovementZ`). After takeover, prefer **virtio-tablet** (absolute X/Y + `EV_REL`/`REL_WHEEL` for VNC scroll); else i8042 AUX (IMPS/2). When tablet is ready, AUX deltas are ignored so the host cursor stays aligned.

## Lifecycle

- Sync poll: `pm_metal_lifecycle_poll` / `lifecycle_focused`.
- Host sets focus on session start; `lifecycle_blur` on session end (unlocks pointer).

## Process (fake WASI process)

Host logical anchor for a live wasm guest — not an async task.

| Concept | Role |
|---------|------|
| **process** | Table entry: id, name, state, optional UI attachment |
| **async task** | Coro/await unit owned by the process’s root async session |
| **UI attachment** | Optional: `none` / `tab` / `fullscreen` (DEFAULT surface) |
| **views** | Shell `ps` today; overlay switcher later |

- API: `include/pymergetic/metal/guest/process/process.h` (`pm_metal_process_*`).
- Guest WASI module `pymergetic.metal.process`: `self`, `info`, `list`, `ui_kind`, `surface`, `tab`.
- `run` / `tab` → `pm_metal_process_spawn_mod`; long-lived async guests stay in the table.
- Sync one-shot mods get a `PID=` for the run then are released (not listed).
- WASI env `PID=<id>` is also injected at instantiate (`getenv("PID")`).
- `pm_metal_wasm_session_*` remains as thin wrappers over the process/async live guest.
- **v1 limit:** one live guest (async session is still global). Multi-process needs per-process async.

---

## Status

Landed: DT, time µs, present, audio null, input pointer/lock + guest `poll_key`, FS write, lifecycle,
stream (uart/ui_tab/pipe/pty + stdio_attach), net lwIP multi-if + virtio/bge + bind_if + stateful DHCPv6,
shell linker-section registry, random/realtime, tab surfaces + clipped present, unlocked UI cursor + tab-strip hit-test,
console scrollback/scrollbar + EFI/BIOS wheel + PageUp/Down,
Ctrl+Shift+Left/Right tab cycle,
`shell_log`/WASI stdout → stdio streams, `gfx_set_surface` for tab-clipped guest draw,
fake process table + `ps` + `PID=` env (single live guest).  
Framebuffer on BIOS i386: Multiboot tag → Bochs → **VESA LFB** (`vesa.c`); x86_64 BIOS still stubs VESA RM. Audio: virtio-snd, else **AC97**, else null. Names/TFTP: hostname + `/etc/hosts` + `pm_metal_net_tftp_get`.  
Open follow-ups: `docs/TODO.md` (mostly iron smoke); multi-process async; overlay view.
