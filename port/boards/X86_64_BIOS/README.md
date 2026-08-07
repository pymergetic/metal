# X86_64_BIOS

Freestanding Multiboot trampoline → long mode → COM1 UART → MicroPython.

```bash
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp run
```

Smoke expects serial `upy ok` and `qemu ok`.
