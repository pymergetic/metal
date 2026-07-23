# EFI Metal — design

Living design for the freestanding UEFI target. Grown in micro-slices:
boot → init → library → WAMR host surface → machine → cut line.

See [LAYERS.md](LAYERS.md) · [src/efi/README.md](../src/efi/README.md).
Hosted ports live only on `archive/multi-host-linux-zephyr-nuttx`.

**Constraint (every runtime slice):** after handover, the hot path never
touches UEFI Boot Services. Allocate, print, time, and I/O go through
Metal-owned code (heap + virtio). Optimize for that path, not for the
boot-services window.

**Memory / SMP:** TLSF for per-CPU (`LOCAL` / `CPU(k)`); custom typed
allocator for `SHARED` — both from the start (`N=1` + shared is fine).
See [COOP_MEMORY.md](COOP_MEMORY.md).

---

## Slice A — power-on until our entry

```text
power → firmware (OVMF) → load metal.efi from ESP → efi_main(ImageHandle, SystemTable)
```

### What firmware does (before us)

- Platform POST / init (not ours).
- Discovers boot media, finds `metal.efi` on the EFI System Partition.
- Loads the PE/COFF image into RAM, relocates it, sets up a call stack.
- Builds `EFI_SYSTEM_TABLE` (Boot Services, Runtime Services, console
  protocols, configuration tables).
- Jumps to our entry with the **MS ABI** on x86_64:

  `EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)`

That is the whole firmware job for bring-up: **load + call**. It is not Metal.

### Handover (two phases)

| Phase | Name | Who is alive |
|-------|------|----------------|
| 1 | Boot-services phase | UEFI Boot Services still usable; we may print, map memory, load files |
| 2 | Owned phase | After `ExitBootServices`; Boot Services are gone; Metal owns RAM and the runloop |

Slice A locks only: firmware loads us and calls `efi_main`. Phase-2 details are Slice B.

### Locked decisions (Slice A)

1. **OVMF is a black box** for the conceptual story (“firmware loads PE, calls us”). ESP layout and QEMU `-drive fat:rw:…` belong in build/verify notes, not in this slice’s narrative.
2. **Entry name stays `efi_main`** (UEFI convention / linker expectation). Metal’s owned runloop is a separate symbol invoked after ExitBootServices (name in Slice B).
3. **First bring-up marker (done):** ConOut banner + memory summary in
   `MetalPkg/main.c` (`metal-efi: ok`). Still Boot Services only —
   ExitBootServices is Slice B.

---

## Slice B — Boot Services init (before ExitBootServices)

Everything here runs once, in phase 1. Goal: snapshot what Metal needs, then
leave firmware behind. Do **not** leave any hot-path dependency on Boot Services.

### Must do before `ExitBootServices`

1. **Keep handles** — store `ImageHandle` and `SystemTable` (or the fields we still need after EBS).
2. **Early console** — `ConOut->OutputString` for the Slice A marker and boot diagnostics.
3. **Memory map** — `GetMemoryMap`; identify usable conventional memory; allocate Metal’s heap pool(s) as `EfiLoaderData` (or equivalent) **before** the final map used for ExitBootServices.
4. **ExitBootServices dance** — get map → `ExitBootServices` → on `EFI_INVALID_PARAMETER`, fetch map again and retry (standard UEFI pattern). On success, Boot Services are dead.
5. **Hand off** — call Metal owned entry (working name: `pm_metal_efi_run`) with: heap base/size, console backend pointer (or “use ConOut only until virtio-console”), and no Boot Services.

### May do in phase 1 (optional for earliest bring-up)

- Read embedded or ESP-resident guest bytes **while** Simple File System still works — or defer and embed guests in the PE image / later virtio-blk.
- Query ACPI/SMBIOS config tables if useful for virt discovery later.
- Record wall-clock via Runtime Services only if we keep a runtime-services dependency; prefer virtio-rtc / NTP later so the owned phase stays lean.

### Must not do after ExitBootServices

- Any Boot Service (`AllocatePool`, `LocateProtocol`, file I/O via UEFI, etc.).
- Rely on ConOut forever if we move to virtio-console (ConOut may still work on some OVMF setups via Runtime; do not count on it for the product path).
- Rely on ConIn / AbsolutePointer — dead after EBS. VNC keys land on i8042; owned shell polls PS/2 set-1 make-codes (`pm_metal_input_ps2_read`) plus COM1/virtio-console RX. Pointer: prefer `virtio-tablet-pci` (absolute, matches VNC); else relative i8042 AUX (host/VNC cursor will drift).
- Rely on ConOut / `Print` / SerialIo — also dead; owned echo goes UI FB + COM1/virtio-console only.

### Performance note

Phase 1 is cold path: correctness and one-time setup. Phase 2 hot path =
allocator + WAMR + virtio MMIO/queues only.

### Locked decisions (Slice B)

- Owned entry after EBS: **`pm_metal_port_takeover_and_run`** → seed init task → **`pm_metal_run_enter`** (BSP main ends; pool is the program).
- Minimum phase-1 success: **ExitBootServices returns success** and logs continue via Metal UART (COM1 / virtio-console), not ConOut/`Print`.

### Boot + unified log (current)

```text
sync floor:
  log_init → mem/stacks → harvest DT/virtio → gfx_harvest → run_init
  print boot tree (mem + cpu + devices)
  APs wait → ExitBootServices → UART attach → seed init → run_enter

pool (seeded init coro, BSP):
  gfx → ui (+ log_attach_ui) → wasm → shell → ready → metal-boot: ready → shell_poll

shutdown (shell `exit` / `quit` / `shutdown`):
  stop shell → wasm → ui → gfx → STOP runners → ResetSystem

net: lwIP (NO_SYS) on virtio-net L2; static IPv4 (SLIRP defaults);
     shell `net` / optional ESP `metal/net.conf`

manual: shell command `test` runs wasm proof suite (not on normal boot)
```

Boot serial is one tree (`pymergetic metal` / `+-- mem` / `+-- devices` / init).  
Headers mirror sources: `include/pymergetic/metal/<group>/<mod>/…`
(`boot/`, `runtime/`, `bus/`, `dev/`, `fs/`, `guest/`, `shell/`).
`dev/<class>/` holds class backends (virtio today; more NICs/GPUs later).
One API: `pm_metal_log` / `pm_metal_logf`, plus styled
`pm_metal_log_styled` / `pm_metal_logf_styled` (`PM_METAL_LOG_STYLE_*`).
Styles are semantic (OK / WARN / FAIL / DIM / ACCENT): UI maps to RGB,
UART/virtio-console to ANSI SGR; UEFI ConOut stays plain.

---

## Slice C — EFI library choice

### Decision

**Intel / Tianocore EDK2 SDK — full package build.** No alternative stack.

Metal’s UEFI application is an **EDK2 package** (DSC/INF, MdePkg + UefiLib /
BaseLib as needed for Boot Services phase). That is how we get correct PE/COFF
entry, calling convention, relocation, and protocol usage — not a DIY
headers-only or gnu-efi path.

| We use | We do not use |
|--------|----------------|
| **EDK2** (pinned under `external/` or setup-fetched) as the build + link system for `metal.efi` | gnu-efi |
| MdePkg / UefiApplication entry (`UefiMain` / package INF) wiring into our `efi_main` or equivalent | Hand-rolled clang `--target=x86_64-unknown-windows` as the primary link story |
| EDK2 libs in **Boot Services phase** for correctness (memory map, console, ExitBootServices) | Inventing our own UEFI CRT / protocol glue “to stay light” |

### Why

- **Correctness first:** UEFI app lifecycle, MS ABI, image handle, system table,
  and ExitBootServices are easy to get subtly wrong without the Intel SDK.
- Protocol and status semantics match what OVMF implements.
- Still keep the global performance constraint: **after ExitBootServices**, the
  WAMR hot path uses Metal heap + virtio — not Boot Services and not a chatty
  UefiLib loop. EDK2 gets us *to* ownership correctly; it does not own the
  runloop.

### Vendoring / setup (when coded)

- Pin a known EDK2 (and edk2-platforms only if required) via `scripts/setup`
  into `external/` (or documented workspace path), same vendoring discipline as
  other externals ([SOURCETREE.md](SOURCETREE.md)).
- Metal package sources stay in-tree under `src/efi/` (and/or an EDK2 package
  dir that points at those sources); `./scripts/build efi` drives `build` /
  `stuart` / edk2 `build` as we wire it.
- Patches to EDK2, if any, go under a revived `patches/edk2/` (none yet).

### Rejected

- **gnu-efi** — not the correctness bar we want.
- **Headers-only + freestanding clang PE/COFF** — rejected; insufficient for a
  trustworthy Boot Services / EBS path.

---

## Slice D — what Metal must offer WAMR (owned phase)

WAMR’s platform layer is the freestanding “OS” WAMR thinks it has. Metal
implements that layer on top of the Slice B heap and Slice E devices.
Shape it for **single-threaded bring-up first**, then grow.

### Required host surface (v1)

| Need | Metal provides | Perf note |
|------|----------------|-----------|
| Heap | `os_malloc` → TLSF `LOCAL`; cross-core types → `SHARED` custom ([COOP_MEMORY.md](COOP_MEMORY.md)) | No UEFI `AllocatePool` after EBS |
| Print | `os_printf` / `os_vprintf` → Metal console write | Buffer then virtio-console; avoid per-char UEFI calls |
| Time | Monotonic ticks (`os_time_*` / usleep-or-yield) | TSC or virtio timer; busy-wait only if no timer yet |
| Memory ops | `memcpy` / `memset` / `memmove` / string basics | Compiler builtins / tiny freestanding libc subset |
| Threads | Stubs or cooperative single thread for v1 | No fake pthreads that block the runloop |
| Atomics / locks | Uniprocessor stubs or real atomics | Prefer real atomics on x86_64 even in ST |
| Abort / panic | Halt or cold reset; message on console | No return to UEFI shell as success |

### Metal runtime on top (later — restore from archive)

Hosted-era `src/common/…/{runtime,memory,mount,port,net,app,util}` is **not**
on this branch; it lives on `archive/multi-host-linux-zephyr-nuttx`. After the
WAMR platform layer works, cherry-pick / restore those modules and re-bind them
under `src/efi/` (contracts documented on the archive as RUNTIME / MEMORY /
MOUNT / WASI).

### Explicitly not offered on the hot path

- UEFI Boot Services
- Full POSIX
- Host filesystem APIs from a hosted OS

### Locked decisions (Slice D)

- **v1 threading model:** one native thread = WAMR runloop (cooperative / no preemptive Metal scheduler yet).
- **Interpreter + AOT:** host builds with `WASM_ENABLE_INTERP` + `WASM_ENABLE_AOT`
  (+ `QUICK_AOT_ENTRY`). Guests may ship `.wasm` (interp) or `.aot` (offline
  `wamrc`); ESP load prefers `mods/apps/<n>/<n>.aot` then `.wasm`. Doom and the
  embed proof `async_aot` use AOT; other embeds stay wasm. Tool: `./scripts/setup wamrc`.
  Product ABI direction: Metal async + freestanding libc (see `docs/LIBC_ASYNC.md`);
  WASI is scaffolding to retire.
- Platform code lives under `src/efi/` (WAMR platform + Metal binds), sharing `src/common/` runtime.

---

## Slice E — machine model, guests, cut line

### Machine (static virtio only)

Order of enablement:

1. **virtio-console** — product serial after EBS (ConOut captured as fallback stub).
2. **virtio-blk** — raw sector device + LBA0 magic proof; package root still ESP pre-EBS.
3. **virtio-net** / **virtio-snd** — Metal-owned; PciIo probe maps BARs to MMIO for post-EBS.

No general PCI driver zoo. Discover virtio-pci (or virtio-mmio on the chosen QEMU machine) with a **fixed, small** probe — not a dynamic driver framework.

Primary bring-up machine: **QEMU + OVMF**, virt-class device set as we adopt virtio.

### Guests

- Guest ABI (current scaffold): wasm + Metal imports from
  `include/pymergetic/metal/dev/{gfx,input,fs}.h`,
  `shell/{ui,shell}.h`, and `runtime/async/async.h` (also via `metal.h`).
  Target ABI:
  Metal async I/O + sync freestanding libc — **not** WASI as product surface
  (`docs/LIBC_ASYNC.md`). UI/async/input are handle-based. Guest await is real
  resume: export `pm_metal_guest_step` + host coro trampoline.
- **Process:** host fake-process table (`guest/process/`) anchors each live wasm
  guest; optional UI attachment (tab / fullscreen). Shell `run`/`tab`/`ps`.
  Guest imports `pymergetic.metal.process` (`self`/`info`/`list`/…) plus WASI
  `PID=<id>` env. Async tasks stay under the process. v1: one live guest
  (global async session). See `docs/IO.md` § Process.
- Proofs: PE-embedded **`hello`** / **`ui_hello`** / **`async_sleep`** /
  **`async_fs`** / **`async_time`** / **`async_net`** / **`async_audio`** /
  **`async_blk`**.
  Verify greps `metal-wasm: t0_hello ok`, `metal-async: sleep|fs|time|net|audio|blk ok`,
  plus `metal-net: virtio-net` / `metal-audio: virtio-snd` when QEMU attaches devices.
- **`doom` parked** (`mods/apps/doom` kept; not built/staged by default).
  Opt-in: `METAL_DOOM_BUILD=1` → `build/doom/`; EFI stages onto ESP,
  BIOS/PXE stages under `build/bios/pxe/mods/apps/doom/` and seeds via HTTP.
- virtio-blk / full package mounts remain later; ESP is the interim package root.

### Cut line — v1 must

- [x] EDK2 builds `metal.efi` and it boots under QEMU/OVMF
- [x] Slice A ConOut marker
- [x] ExitBootServices + owned runner pool (`pm_metal_port_takeover_and_run`)
- [x] Slim WAMR (interp + libc-wasi) over Metal heap; WASI stdout → UI tab
- [x] Run embedded wasm hello via shell / auto-init
- [x] `./scripts/verify efi` watches for agreed success strings

### Cut line — v1 must not

- Full suite (util natives, multi-module, HTTPS)
- Dynamic PCI / non-virtio device zoo (virtio-net/snd are intentional fixed probes)
- Restoring hosted linux/zephyr/nuttx ports on this branch

### Build & verify (human signal)

```bash
./scripts/setup.d/port/efi/default.sh
./scripts/build efi          # → metal.efi when link works
./scripts/verify efi         # QEMU + OVMF; grep success marker
```

Exact QEMU flags and marker strings land with the implementation; this doc
owns the **story**, not the flag soup.

---

## Slice order vs coding order

Design lock order (this doc): A → B → C → D → E.

Suggested coding order once this doc stays stable:

1. EDK2 setup + Metal UEFI package → ConOut marker (A)
2. Memory map + ExitBootServices + `pm_metal_efi_run` (B)
3. Allocator + console stub + WAMR platform (D)
4. Embedded hello wasm
5. virtio-console → virtio-blk (E)

---

## Open only when coding (not design blockers)

- Exact `pm_metal_efi_run` signature and heap layout constants
- EDK2 toolchain flavor (`build` vs stuart) and package path layout under `src/efi/`
- virtio-pci vs virtio-mmio for the first QEMU machine type
- Whether early guests are PE-embedded or read from ESP before EBS
