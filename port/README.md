# metal/port

MicroPython **port** only: boards, HAL, upy knobs, image boot/bringup/smoke,
seat variants. Product faces live beside this tree:

- `../include/` · `../src/` — C ABI + callees  
- `../glue/` · `../typings/` — µPy nest + stubs (path == module)  
- Frozen CORE Py (inspect/arch/…) from `../src/` via `manifest*.py` (nest builtins own parents)

Build outputs land under [`build/`](build/) (one tree per board/engine/mode).

Netboot (OpenWrt / SSH): [`../deploy/`](../deploy/) — `bootserver/` +
`upload-bootserver` (iPXE + config; image from metal-cdn).
LIVE firmware proofs: [`live/`](live/).

```bash
# CI smoke battery
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp run
make -C ports/metal BOARD=X86_64_UEFI ENGINE=mp run

# Product REPL (lean bring-up + friendly µPy on COM1)
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp repl
make -C ports/metal BOARD=X86_64_UEFI ENGINE=mp repl
```

## µPy modules (wired from this port)

| Import | Glue | Stub |
|--------|------|------|
| `pymergetic.metal.util.lz4` | `../glue/.../util/lz4.c` | `../typings/.../util/lz4.pyi` |
| `pymergetic.metal.net.ssh` | `../glue/.../net/ssh.c` (firmware) | `../typings/.../net/ssh.pyi` |
| `network.LAN` | `bringup/network_metal_nic.c` | — |
| `socket` / `framebuf` | upstream µPy + Metal NIC | — |

