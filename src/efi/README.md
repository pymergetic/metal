# Freestanding EFI target (Metal as firmware)

UEFI application that runs WAMR/Metal with **no hosted OS**.
I/O policy: **static virtio only** (console, then blk, then net) — not a
general PC driver zoo.

## Goals

- Boot under QEMU/OVMF (and later Firecracker-class virt) as `metal.efi`
- After `ExitBootServices`: Metal owns heap, timers, and the runloop
- Same guest ABI as other ports (packages, WASI, mods) where possible
- Hosted ports (linux / zephyr / nuttx) stay for real hardware breadth

## Non-goals (for now)

- FreeDOS / POSIX-on-DOS
- Dynamic PCI drivers beyond virtio
- Replacing Zephyr/NuttX MCU paths

## Layout (bring-up)

```
src/efi/
  README.md          — this file
  CMakeLists.txt     — freestanding / PE/COFF EFI app
  main.c             — UEFI entry → Metal
  virtio/            — static virtio-pci (later)
```

## Build

```bash
./scripts/setup.d/port/efi/default.sh   # toolchain notes / deps
./scripts/build efi
./scripts/verify efi                    # QEMU + OVMF smoke (later)
```

Requires a UEFI-capable toolchain (see setup script). Until the PE/COFF
link works end-to-end, `scripts/build efi` reports what is missing.

## Related archive

Multi-host bring-up (linux / zephyr / nuttx qemu) is on branch
`archive/multi-host-linux-zephyr-nuttx`.
