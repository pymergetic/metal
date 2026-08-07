# metalmod/port

MicroPython boards for Metal. Entry: `ports/metal`.

```bash
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp run

# Engines (sibling trees under packages/)
make -C ports/metal BOARD=X86_64_BIOS ENGINE=upy run
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mpwm run
```

| Board | Status |
|-------|--------|
| `X86_64_BIOS` | Multiboot + COM1 + µPy (`print('upy ok')` smoke / REPL) |
| `X86_64_UEFI` | Banner PE (µPy next) |

`ENGINE=mp|mpwm|upy` selects the µPy tree. Default smoke runs `print('upy ok')` then isa-debug-exit. Interactive REPL: build with `-DMETAL_UPY_SMOKE=0` (forthcoming `REPL=1` flag).

Port face only under `port/`; muscles stay in `extmod/metalmod/*` (rename → `metal` planned).
