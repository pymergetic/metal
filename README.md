# pymergetic-metal

Zephyr OS core for Pymergetic: memory layout, portable `.o` loading, `pm_port` glue.

**Not here:** CPython, zlib, OpenSSL, etc. — those belong in [`packages/kernel`](../kernel/).

**Fake metal** — `native_sim`, fast dev on Linux.  
**Real metal** — QEMU x86_64 / hardware.

## Layout

```
src/pymergetic/metal/     tracked source (app, module, port, ram, mod)
scripts/                  env.sh, setup-west, build, west
third_party/zephyr/       pinned Zephyr kernel (submodule)
.west/config              west workspace pointer (committed)
```

Gitignored local Zephyr workspace (created by `setup-west` / builds — normal for any Zephyr project):

```
modules/ bootloader/ tools/   west update
build/zephyr/                west build output
.venv/                       west + python deps
```

## First-time setup

```bash
cd packages/metal
git submodule update --init third_party/zephyr
./scripts/setup-west
source scripts/env.sh
```

Requires [Zephyr SDK](https://github.com/zephyrproject-rtos/sdk-ng) on `PATH` (or `ZEPHYR_SDK_INSTALL_DIR`).

**Zephyr IDE / VS Code:** import west workspace root = **`packages/metal`** (this directory).

## Build hello world (fake metal)

```bash
source scripts/env.sh
./scripts/build -p always -b native_sim/native/64 app
timeout 5 "${ZEPHYR_BUILD}/zephyr/zephyr.exe"
```

Expected:

```
Hello from pymergetic-metal on native_sim/native/64
  pymergetic/metal: ok
```

## Build hello world (QEMU x86_64)

```bash
source scripts/env.sh
./scripts/build -p always -b qemu_x86_64 app
./scripts/west build -t run
```

## Scope

| Package | Responsibility |
|---------|----------------|
| **metal** (this) | Zephyr app, RAM policy, mod loader, port layer |
| **kernel** | CPython 3.14, vendored C libs, language runtime |

Metal requirements: `REQUIREMENTS.md`. Python port checklist: `../kernel/REQUIREMENTS_PYTHON.md`.
