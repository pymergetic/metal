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

### Performance note

Phase 1 is cold path: correctness and one-time setup. Phase 2 hot path =
allocator + WAMR + virtio MMIO/queues only.

### Locked decisions (Slice B)

- Owned entry after EBS: **`pm_metal_efi_run(...)`** (parameters refined when coding).
- Minimum phase-1 success after Slice A marker: **ExitBootServices returns success** and `pm_metal_efi_run` can print via a Metal-owned console stub (even if that stub still wraps a captured ConOut pointer for early bring-up).

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
- **Fast interpreter** (`WASM_ENABLE_FAST_INTERP=1`) for bring-up; AOT (`wamrc` +
  `WASM_ENABLE_AOT`) is the next performance lever once the doom path is solid.
- Platform code lives under `src/efi/` (WAMR platform + Metal binds), sharing `src/common/` runtime.

---

## Slice E — machine model, guests, cut line

### Machine (static virtio only)

Order of enablement:

1. **virtio-console** — product console; replaces ConOut dependency for owned phase.
2. **virtio-blk** — guest packages / root image.
3. **virtio-net** — later; not required for first guest hello.

No general PCI driver zoo. Discover virtio-pci (or virtio-mmio on the chosen QEMU machine) with a **fixed, small** probe — not a dynamic driver framework.

Primary bring-up machine: **QEMU + OVMF**, virt-class device set as we adopt virtio.

### Guests

- Guest ABI: `wasm32-wasip1` plus Metal WASI-style imports from
  `include/pymergetic/metal/{gfx,ui,shell,async,input}.h`. UI/async/input are
  handle-based (no host pointers across the boundary). Guest await is real
  resume: export `pm_metal_guest_step` + host coro trampoline.
- Proofs: PE-embedded **`hello`** / **`ui_hello`** / **`async_sleep`**; ESP
  package **`doom`**:
  ```text
  build/efi/esp/
    EFI/BOOT/BOOTX64.EFI
    mods/apps/doom/doom.wasm
    mods/apps/doom/doom1.wad
  ```
  Guest loads IWAD through `pymergetic.metal.fs` (size + read into wasi-libc
  malloc) and WASI preopen `/` for existence probes. Long-lived async sessions
  (`pm_metal_guest_step` + `await(sleep ~28ms)`, doomgeneric `singletics`,
  integer-scale centered blit) are pumped from `shell_poll`; shell chrome is
  not redrawn while game focus is on. Headless verify rebuilds doom with
  `METAL_DOOM_MAX_TICKS=120` plus ESP marker `mods/apps/doom/autostart`, and
  greps `metal-wasm: t0_hello ok`, `metal-async: sleep ok`, `metal-doom: ok`.
  Interactive: `METAL_DOOM_MAX_TICKS=0`, no autostart — `./scripts/run efi` +
  VNC, then `run doom` / `tab doom`.
- virtio-blk / full package mounts remain later; ESP is the interim package root.

### Cut line — v1 must

- [x] EDK2 builds `metal.efi` and it boots under QEMU/OVMF
- [x] Slice A ConOut marker
- [ ] ExitBootServices + `pm_metal_efi_run`
- [x] Slim WAMR (interp + libc-wasi) over Metal heap; WASI stdout → UI tab
- [x] Run embedded wasm hello via shell / auto-init
- [x] `./scripts/verify efi` watches for agreed success strings

### Cut line — v1 must not

- Full suite (util natives, multi-module, HTTPS)
- virtio-net
- Dynamic PCI / non-virtio device zoo
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
