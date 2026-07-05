# pymergetic-metal

Zephyr application shell for Pymergetic.

**Fake metal** — `native_sim`, fast dev on Linux.  
**Real metal** — QEMU x64 / hardware.

Port layer (`pm_port`) hides Zephyr. Kernel mods compile to portable `.o` against the kernel API; this repo loads or glues them. Vendor Zephyr lives in an external west workspace — not here.

## Layout (planned)

```
app/          Zephyr application entry
port/         pm_port implementation
boards/       fake + real defconfigs
scripts/      west wrappers, optional glue step
```

## Modes

| | fake metal | real metal |
|---|------------|------------|
| Board | `native_sim` | `qemu_x86_64` / HW |
| `CONFIG_PM_DEV` default | on | off |

Dev (JIT, dynload, tooling) is a separate Kconfig menu.
