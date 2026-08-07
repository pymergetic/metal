# X86_64_BIOS (metalmod)

Freestanding Multiboot trampoline + COM1 banner. No forge.

```bash
make -C ports/metal BOARD=X86_64_BIOS
make -C ports/metal BOARD=X86_64_BIOS run
# → qemu-system-x86_64 -kernel build-X86_64_BIOS/metal.qemu.elf
```

Serial: COM1 @ 115200. QEMU uses `isa-debug-exit` at `0x501`.
