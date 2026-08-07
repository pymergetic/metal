# metal/port

Thin µPy board face. Muscles live beside this tree under `extmod/metal/{mem,async,console,…}`.

```bash
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp run
make -C ports/metal BOARD=X86_64_UEFI ENGINE=mp run
```

## µPy modules

| Module | Impl | IDE stub |
|--------|------|----------|
| `network.LAN` | `common/network_metal_nic.c` | — |
| `socket` / `framebuf` | upstream µPy + Metal NIC | — |
| `ssh` | `common/modssh.c` | `typings/ssh.pyi` |

See `typings/README.md` for pyright `stubPath` setup.
