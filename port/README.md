# metalmod/port

MicroPython-style boards for Metal. Thin entry: `ports/metal`.

```bash
make -C ports/metal BOARD=X86_64_BIOS
make -C ports/metal BOARD=X86_64_BIOS run   # qemu-system-x86_64 -kernel …

make -C ports/metal BOARD=X86_64_UEFI
make -C ports/metal BOARD=X86_64_UEFI run   # OVMF + ESP fat:
```

| Board | Status |
|-------|--------|
| `X86_64_BIOS` | Multiboot trampoline + COM1 banner (qemu `-kernel`) |
| `X86_64_UEFI` | `UefiMain` PE + COM1 banner (OVMF) |

No forge. Next: embed µPy REPL / `ENGINE=` matrix.
