# pymergetic-metal

Zephyr OS core for Pymergetic: memory layout, portable `.o` loading, host/plugin API glue.

**Not here:** CPython, zlib, OpenSSL, etc. — those belong in [`packages/kernel`](../kernel/).

**Fake metal** — `native_sim`, fast dev on Linux.  
**Real metal** — QEMU x86_64 / hardware.

## Layout (today)

```
runtime/
  zephyr/               Zephyr application (hello world, metal on fake/real metal)
    prj.conf            app Kconfig
    src/                application source
    boards/             RAM overlays + board conf fragments
    build/              west build output (local, not committed)
  linux/                Linux host runtime (placeholder)
    config/
    src/
external/zephyr/          Zephyr v4.4.0 (west update, gitignored tree)
west-manifest/west.yml    west manifest (pinned kernel revision)
.west/config              west workspace pointer (committed)
.venv/                    Python venv with west (local)
.vscode/zephyr-ide.json   Zephyr IDE project (board, build dir, extra Kconfig)
```

Gitignored by west / setup (normal for Zephyr workspaces): `external/modules/`, `external/bootloader/`, `external/tools/`, `.venv/`, `.cache/`.

include/pymergetic/       Public headers only (metal host API, export mod SDK)
src/pymergetic/           Implementation + private headers beside .c files

## First-time setup

```bash
cd packages/metal
west update
python3 -m venv .venv
source .venv/bin/activate
pip install west
pip install -r external/zephyr/scripts/requirements-base.txt
```

Requires [Zephyr SDK](https://github.com/zephyrproject-rtos/sdk-ng) on `PATH` (or `ZEPHYR_SDK_INSTALL_DIR`).

**Zephyr IDE / VS Code:** open workspace root = **`packages/metal`**. Active app project: **`runtime/zephyr`**, board profile **`native_sim/native/64`** (see `.vscode/zephyr-ide.json`).

## Build hello world (fake metal)

Via Zephyr IDE: build the `runtime/zephyr` project for `native_sim/native/64`.

Or from the shell:

```bash
source .venv/bin/activate
west build -b native_sim/native/64 runtime/zephyr \
  -p --build-dir runtime/zephyr/build/native_sim/native/64
timeout 5 runtime/zephyr/build/native_sim/native/64/zephyr/zephyr.exe
```

Expected output:

```
Hello from pymergetic-metal on native_sim/native/64
  pymergetic/metal: ok
```

## Configuration

Kconfig merge order:

1. Board defconfig (`external/zephyr/boards/native/native_sim/native_sim_64_defconfig`, …)
2. `runtime/zephyr/prj.conf`
3. CMake `-DCONFIG_*` from Zephyr IDE / west (e.g. debug options in `zephyr-ide.json`)

After a build, the full merged config is:

```
runtime/zephyr/build/<board>/zephyr/.config
```

Inspect with `rg '^CONFIG_.*=y' runtime/zephyr/build/.../zephyr/.config` or `west build -t menuconfig`.

## Build hello world (QEMU x86_64)

```bash
source .venv/bin/activate
west build -b qemu_x86_64 runtime/zephyr \
  -p --build-dir runtime/zephyr/build/qemu_x86_64
west build -d runtime/zephyr/build/qemu_x86_64 -t run
```

## Scope

| Package | Responsibility |
|---------|----------------|
| **metal** (this) | Zephyr app, RAM policy, mod loader, port layer |
| **kernel** | CPython 3.14, vendored C libs, language runtime |

Roadmap: `REQUIREMENTS.md`. Memory design: [docs/MEMORY_MODEL.md](docs/MEMORY_MODEL.md). Python port checklist: `../kernel/REQUIREMENTS_PYTHON.md`.
