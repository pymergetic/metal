# Freestanding EFI target (Metal as firmware)

UEFI application built with the **Intel/Tianocore EDK2 SDK**
([docs/EFI.md](../../docs/EFI.md) Slice C). I/O policy later: static virtio
only. Today: Boot Services hello (banner + memory map).

## Layout

```
src/efi/
  README.md
  MetalPkg/           EDK2 package (PACKAGES_PATH)
    MetalPkg.dec / .dsc / Metal.inf
    main.c            entry: banner + claim RAM + mem smoke
    mem/              coop allocator (TLSF LOCAL + SHARED slabs)
```
See docs/COOP_MEMORY.md.
## Build / verify

```bash
./scripts/setup edk2    # once — external/edk2 + .tools/nasm + BaseTools
./scripts/build efi     # → build/efi/metal.efi
./scripts/verify efi    # QEMU + OVMF; greps banner / Total memory / ok
```

## Related archive

Hosted ports: `archive/multi-host-linux-zephyr-nuttx`.
