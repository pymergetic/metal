# Typings (Metal µPy port)

Pyright / Pylance stubs for built-in modules not in upstream MicroPython.

| Module | C impl | Notes |
|--------|--------|-------|
| `ssh` | `modssh.c` | `net.ssh` hybrid face (`pm_metal_net_ssh_*`; stub) |

Add `extmod/metal/port/typings` to your IDE `stubPath` (see `ports/metal/README.md`).

See `ports/metal/README.md` for smoke serial markers.
