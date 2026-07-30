# exp2 platform floor (BIOS / EFI)

Product firmware is **inspiration** (boot sequence, mem ideas, Multiboot / EFI
facts). It is not a file checklist. In particular, do **not** recreate
`src/bios/shim/` fake UEFI (`PmBiosUefi.h`, `UINT32`, `Library/IoLib.h`).

## Goal

One symmetric, stdint-typed **ops-struct** surface for everything that must
differ between firmware targets. Shared code (C, then Rust, then Python) never
sees COM1 ports or `Uefi.h`.

**Ideal: only `boot/` diverges.** All other modules
(`mem`, later net, …) are target-agnostic and call boot ops (or higher
shared APIs). No `runtime/` nesting in exp2 — modules sit flat under
`pymergetic/metal/`. Platform code is under `boot/<target>/` only. If
something seems to need a second copy elsewhere, push a new boot ops slot
(or a new shared module) instead of forking.

### Sealed API (nothing firmware-specific leaks)

Public ops headers under `boot/platform/` (and other generated pool faces)
must be **completely target-agnostic**:

- No EFI / BIOS / Multiboot / EDK2 names in public types or required comments.
- No `UINT*`, `EFI_*`, COM1 ports, Multiboot magics in those headers.
- Callers see only Metal names (`pm_metal_boot_*`, …).
- Target details only under `boot/platform/<target>/**`. Shared / generated
  code never `#include`s `boot/platform/bios/` or `boot/platform/efi/`.

## Tree

```text
exp2/src/pymergetic/metal/boot/          # orchestration + harvest
  platform/                              # target ops ONLY (uart half, mem_map, …)
    uart.h  mem_map.h  power.h  handoff.h
    private/                             # type=hidden — shared BIOS/EFI bring-up
    bios/ efi/                           # type=hidden — lower-half + thin main

exp2/src/pymergetic/metal/console/       # CONCEPT — ring + viewports (not hardware)
exp2/src/pymergetic/metal/dev/serial/    # HARDWARE — unified serial over platform uart
exp2/src/pymergetic/metal/dt/
exp2/src/pymergetic/metal/mem/
```

Target `main` stays thin (ingest + debug-exit). Shared path after the floor
is `platform/private/bringup.c` (`pm_metal_boot_bringup`).

Human impl and generated projections share the module root; schema lives
under `.pm/`. Codegen is gated by the in-file ownership banner — see
[`docs/definitions/module.md`](../../docs/definitions/module.md).

Build links **either** `boot/platform/bios/*.c` **or**
`boot/platform/efi/*.c` for a given image — never both.

### Platform target (secure for N platforms)

More firmware floors will show up later (`bios`, `efi`, …). Scale by
**directory + required build define**, not by `#ifdef`ing every line so one
binary swallows the tree.

Rules:

1. One subdirectory per platform: `boot/platform/<target>/` (e.g. `bios`, `efi`).
2. Build **must** set exactly one of:
   `-DPM_METAL_BOOT_TARGET_BIOS=1` or `-DPM_METAL_BOOT_TARGET_EFI=1`
   (later: `…_FOO=1`). Never two at once.
3. Build compiles/links only `boot/platform/<that-target>/**` — same getter
   symbols on every platform; one definition per image.
4. Each ops `.c` starts with a mismatch tripwire (secure against a wrong
   file in SRCS):

```c
#if defined(PM_METAL_BOOT_TARGET_BIOS)
/* ok — this file is boot/platform/bios/... */
#elif defined(PM_METAL_BOOT_TARGET_EFI) || defined(PM_METAL_BOOT_TARGET_*)
#error "boot/bios/*.c built with non-BIOS PM_METAL_BOOT_TARGET_*"
#else
#error "PM_METAL_BOOT_TARGET_* is not defined"
#endif
```

(Same idea under `boot/platform/efi/`, inverted.)

| Approach | Verdict |
|----------|---------|
| Dir per target + build selects that dir | **Yes** |
| Required `PM_METAL_BOOT_TARGET_*` + `#error` tripwire in each ops `.c` | **Yes** — cheap insurance |
| One CI/config per target | **Yes** |
| One link of every `boot/platform/*/` at once | **No** — duplicate symbols |
| Heavy `#ifdef` bodies to compile all platforms in one TU set | **No** |

| Path | Dialect |
|------|---------|
| `boot/platform/*.h` | ISO C / `stdint` only |
| `boot/platform/bios/**` | ISO C / `stdint` only — **no** fake UEFI types |
| `boot/platform/efi/**` | Real EDK2 allowed; translate at the ops boundary |
| Everything else under `src/pymergetic/metal/**` | Shared; ISO C / `stdint`; **no** per-target fork |
| Entry asm/ld | Under `boot/platform/<target>/` (e.g. crt0/link); call stdint main |

**Dialect note vs product:** product keeps EDK2 only under top-level
`src/efi/**`. Exp2 scopes the EFI exception to `…/boot/platform/efi/**`.
Still never EDK2 in `boot/platform/*.h` or `boot/platform/bios/**`.

No fake UEFI shim anywhere. New firmware code goes under
`pymergetic/metal/boot/platform/<target>/` (or shared modules), nowhere else.

## Ops style

Follow the product ops-struct habit ([`docs/SOURCETREE.md`](../../docs/SOURCETREE.md)):
struct-of-function-pointers, `static const` table in the target `.c`, getter
returns its address for the process lifetime. `NULL` slot = this module has no
such operation; a required-but-unimplemented slot gets a real stub, not a
NULL field.

Port contracts live under `boot/platform/` (human C module; sync emits
`.rs` / `.pyi`):

- `uart.h` — **lower half only** (target poke: COM1 outb / EFI SerialIO, …)
- `io.h` — port I/O (`inb`/`outb`/`in32`/`out32`) for PCI cfg / ISA probes
- `mem_map.h`, `power.h`, `handoff.h`
  (no `sync`/CAS ops — locks use language/core atomics directly;
  do not reintroduce a boot CAS table unless callers actually use it)

Bios/efi implement those. **Never** put a device driver or a console ring
in `boot/platform/`. Other module faces come from `metal mod sync`.

Product [`boot/port.h`](../../include/pymergetic/metal/boot/port.h) (takeover /
framebuffer) is a **later** ops group if needed. Do not overload it for early
I/O or memory map.

## v1 surface (hello + mem)

| Group | Slots |
|-------|--------|
| uart (platform) | lower half: `write` bytes — ASCII on the wire |
| io (platform) | `outb` / `inb` / `out32` / `in32` |
| mem_map | `get(regions, max, *n)`, `image_end() -> uintptr_t` |
| power | `halt()`, `reset(int32_t reboot)` |
| handoff | `leave_firmware()` |

## Boot vs DT

**Metal DT** (device / capability table — not Linux FDT; see
[`docs/IO.md`](../../docs/IO.md)) is its own kernel module:

```text
exp2/src/pymergetic/metal/dt/     # .module — sealed add/lookup/walk
```

Not under `boot/`. Boot only **orchestrates** early harvest (and target
floor seeds); drivers in `bus/*` / `dev/*` **add** and later **bind** via
`dt` APIs. Lifetime of the table outlasts “boot phase,” so burying it
under `boot/` would couple every consumer to boot.

### Still inside `boot` (firmware floor + orchestration)

| Area | Role | When |
|------|------|------|
| **uart** (platform lower half) / **mem_map** / **power** | Target ops (v1) | v1 |
| **harvest** | Run linked detectors → they call into `dt` | after dt module exists |
| **handoff** | Leave EFI BS / BIOS services | after early console + serial viewport; before full DT harvest |
| **framebuffer** / early **time** (optional ops) | Floor describe / calibrate | as needed |

### Same scheme cluster (landed shape)

| Module | Role |
|--------|------|
| **`boot`** | Orchestration + harvest; `platform/` = target ops only |
| **`console`** | Virtual console = ring + viewports (**concept**, not hardware) |
| **`dev/serial`** | Serial **hardware**; unified API over platform uart half |
| **`dt`** | Device/capability table (Rust) |
| **`mem`** | Host heap |

`dev/*` is **hardware only**. No rings under `dev/`.

### Layers (clean)

```text
metal/console/              # CONCEPT — ring buffer + viewport attach
       ^
       | attach viewport
metal/dev/serial/           # HARDWARE — unified serial device API
       ^
       | uses
metal/boot/platform/uart.*  # LOWER HALF ONLY — target poke (bios/efi)
```

| Piece | Path | Is |
|-------|------|-----|
| Console | `…/metal/console/` | Buffer / concept. Not a device. Not under `boot/` or `dev/`. |
| Serial | `…/metal/dev/serial/` | Hardware. Unified access for COM1 / EFI serial / … |
| UART half | `…/boot/platform/uart.h` + `bios\|efi` | Target poke only. No driver logic. No “console” name. |

**Forbidden:** calling the platform uart half “console”; putting serial under
`boot/platform/`; putting the console ring under `dev/` or `boot/platform/`.

Today’s misnamed `boot/platform/console.*` **dies** — becomes `uart.*`
(lower half). Callers go through `dev/serial`, not the ops header directly
(except serial itself).

### Console (concept)

**Console** = virtual ring + viewport attach points. Can come up with
**zero viewports** — writes still accept; bytes stay in the ring until a
viewport exists. Keep the ring (and the **beginning** position) so a
viewport that attaches later can join and replay history from the start.
Console #0 is first after mem (heap for the ring); lives until the system
dies.

**All I/O through viewports.** No bypass of the ring to poke hardware.
Output drains only via attached viewports; input enters only via
viewports. Serial being up does **not** make it a viewport — attach is
**manual** (boot orchestration calls `console.attach`).

**Viewport** = concrete sink/source (`dev/serial`, virtio-console, UI tab,
SSH, …) explicitly attached to one console.

**Multi-console, multi-viewport:** many consoles; “early” is just #0.
Each console fans out to multiple viewports; each new attach can replay
from the retained beginning.

**Hybrid sync / async:** early sync put into ring; async later on the
same objects. Never tear down #0 at async bring-up; never require serial
before console #0 exists.

See [`docs/IO.md`](../../docs/IO.md) (sync vs async).

Floor path (sync until async start):

```text
target entry (bios|efi)
  -> mem_map ingest (+ power ops available)
  -> mem claim + init                         # heap
  -> console #0 create/init (ring, 0 viewports)
  -> log default -> console 0
  -> banner (buffered on ring)
  -> dt.reset
  -> dt.seed_mem (sysmem + bound heap)        # class MEM — partitioning intel only
  -> dt.seed_bound_uart (COM1 / EFI serial)   # CAP_BOUND bookkeeping
  -> dump_mem (walk DT MEM -> console/log)
  -> dev/serial up (platform uart half)
  -> console.attach(serial)                   # ring drains
  -> handoff.leave_firmware                   # NOT async start
  -> boot_harvest: each linked *_detect()     # skip CAP_BOUND
       # detectors live in drivers (bus/*, dev/*);
       # harvest only calls them — no central hardware table
       bus/pci, time, acpi, random, input, blk, gfx, ...
  -> hwtree print (from DT [+ ACPI RSDP if found])
  -> ... more sync bring-up ok here ...
  ---------- async start (runners) ----------  # STOP sync-only tranche
  -> awaitable blk/input/gfx/net/...
```

**Harvest rule:** each driver exports `pm_metal_*_detect()` that probe /
identify and `dt_add` only (no blocking I/O). Already-`CAP_BOUND` nodes
(e.g. floor UART, bound heap) are skipped — not re-inited.

**Why this order:** heap → console ring (logs before serial exists) →
banner buffered → DT seed + mem dump → serial viewport drain → handoff →
driver detect harvest → hwtree. Sync work may continue after handoff up to
the async-start gate.

Path of a post-mem write: **caller → console ring → viewports**.
Never “console = COM1.”

Landed shape: `platform/uart.*` / `io.*`, `console/`, `dev/serial/`,
`bus/pci/`, `hwtree/`; `main` follows the floor path (manual serial
viewport attach).

## Polyglot (`.module` / flat root)

See [`docs/definitions/module.md`](../../docs/definitions/module.md). Boot splits:

| Path | Marker | Role |
|------|--------|------|
| `boot/<target>/` (`bios`, `efi`, …) | **`.nomodule`** (required) | Plain port TUs (+ asm). Not a polyglot module. Human-only. |
| `boot/` (module root) | **`.module`** | Kernel module — `{base}.rs` + `catalog.toml` + ports |

`boot` is a **kernel module** (`.module`): built **into** the firmware
image. Target ports under `.nomodule` dirs are just the bind bodies that
module needs; they are not separate modules.

### Markers (kernel vs package vs plain)

| Marker | Meaning |
|--------|---------|
| `.module` | **Kernel module** — linked into the firmware. Codegen OK when marked. |
| `.package` | **Package** — WASI / `metal pack` wasm. Same layout; **not** in the kernel link unless integrated. |
| `.nomodule` | **Plain tree** — forbid module/package treatment and codegen (e.g. `boot/bios/`). |

A directory has at most one of `.module` / `.package` / `.nomodule`.
Mixing is an error.

**`.nomodule`** — explicit forbid. `metal mod sync` / `check` / pack must
**refuse** to treat it as a module.

Rules:

- Ops **tables / getters** (`pm_metal_boot_*_ops`) are always defined in
  `boot/<target>/*.c` as `extern "C"`. That stays the firmware bind.
- Shared boot body is **Rust** at module root (`{base}.rs`): wrappers /
  orchestration calling the C ops ABI only. Do not put bios/efi under
  the Rust crate as if they were the impl language.
- Codegen writes public projections only through the banner write gate.
  Never write under a `.nomodule` subtree (today: `common/`, `bios/`, `efi/`).
- Target private headers stay under `boot/<target>/`.

```text
boot/
  .module
  catalog.toml
  {base}.rs
  boot.h …             # GENERATED (banner-owned)
  common/              # human port ops (bios+efi)
  bios/
    .nomodule
    *.c
  efi/
    .nomodule
    *.c
```

## Language ladder

1. **Target C ports** — `boot/bios` (later `boot/efi`) provide ops getters.
2. **Shared Rust** — `boot` + `dt` + `mem` `impl = rs`; `catalog.toml` → C headers.
3. **Rust onward** for new shared Metal logic: use each module's own faces
   (`boot/platform/*.rs`, `dt` / `mem` generated faces) — do not re-export
   platform ops through `boot` (keeps Rust aligned with `boot/__init__.h`).
4. **`mem`** — arena + TLSF in Rust (done; no `tlsf_edk2` / host_stubs).
5. **upy** — register/reexport at interpreter startup (attach-only).

## Rejected

- Any EFI/BIOS/Multiboot/EDK2 symbol or type in `boot/*.h` or other shared APIs
- `PmBiosUefi.h` / dual spellings (`UINT32` vs `uint32_t`) under BIOS
- Fake `Library/*`, `Protocol/*` shim headers as a second EDK2
- Shared C that includes headers from `boot/<target>/`
- Compiling product `bios/default.sh` or overlay fallthrough to `../src`
- Putting target-specific code outside `boot/<target>/`

## Inspired by product (ideas only)

- Multiboot entry + mmap claim above image end
- Early path: console ring then `dev/serial` viewport (platform uart half)
- Dual-span arena + TLSF as the mem endgame (implementation may start simpler)
- Ops-struct bind pattern from SOURCETREE
