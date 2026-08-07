# metal/port

Thin µPy board face. Muscles live under `extmod/metal/{include,src}/…`.

```bash
# CI smoke battery
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp run
make -C ports/metal BOARD=X86_64_UEFI ENGINE=mp run

# Product REPL (lean bring-up + friendly µPy on COM1)
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp repl
make -C ports/metal BOARD=X86_64_UEFI ENGINE=mp repl
```

## µPy modules

| Module | Impl | IDE stub |
|--------|------|----------|
| `network.LAN` | `common/network_metal_nic.c` | — |
| `socket` / `framebuf` | upstream µPy + Metal NIC | — |
| `ssh` | `common/modssh.c` | `typings/ssh.pyi` |

See `typings/README.md` for pyright `stubPath` setup.
