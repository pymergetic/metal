# pymergetic-metal

Zephyr application shell for Pymergetic.

**Fake metal** — `native_sim`, fast dev on Linux.  
**Real metal** — QEMU x64 / hardware.

Port layer (`pm_port`) hides Zephyr. Kernel mods compile to portable `.o` against the kernel API; this repo loads or glues them.

## third_party (pinned submodules)

| Source | Tag | Path |
|--------|-----|------|
| [python/cpython](https://github.com/python/cpython) | `v3.14.6` | `third_party/cpython/` |
| [zephyrproject-rtos/zephyr](https://github.com/zephyrproject-rtos/zephyr) | `v4.4.0` | `third_party/zephyr/` |

```bash
git submodule update --init --recursive
```

Zephyr HAL/modules still come from `west update` with `third_party/zephyr` as manifest root (separate west workspace dir for the rest).

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
