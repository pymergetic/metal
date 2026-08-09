# X86_BIOS

i686 Multiboot ELF32 (load at 1MiB) → protected mode → COM1 UART → MicroPython.

```bash
make -C ports/metal BOARD=X86_BIOS ENGINE=mp LINK_WAMR=0 run
```

Product image is a single `metal.elf` (`metal.qemu.elf` is the same).
