# Freestanding EFI target (Metal as firmware)

UEFI application built with the **Intel/Tianocore EDK2 SDK**
([docs/EFI.md](../../docs/EFI.md) Slice C). I/O policy later: static virtio
only. Today: Boot Services bring-up (claim RAM, dual-span mem, runloop/task smoke).

## Layout

```
src/pymergetic/metal/     module contracts (pm_metal_<module>_*)
  arena/  mem/  task/  run/  stack/  util/

src/efi/
  README.md
  pymergetic/metal/       EFI binds (UEFI / EDK2 impl)
    arena/  mem/  task/  run/  stack/
  MetalPkg/               EDK2 package (PACKAGES_PATH)
    MetalPkg.dec / .dsc / Metal.inf
    main.c                UefiMain: banner + claim RAM + smoke
```

See [docs/COOP_MEMORY.md](../../docs/COOP_MEMORY.md).

## Build / verify

```bash
./scripts/setup edk2    # once — external/edk2 + .tools/nasm + BaseTools
./scripts/build efi     # → build/efi/metal.efi
./scripts/verify efi    # QEMU + OVMF; greps banner / Total memory / ok
```

## Related archive

Hosted ports: `archive/multi-host-linux-zephyr-nuttx`.
