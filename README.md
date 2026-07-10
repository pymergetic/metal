# pymergetic-metal

Pymergetic orchestrator metal: memory layout, mod loading, and host/guest stack on Zephyr (ship) and Linux (dev).

**Not here:** CPython, zlib, OpenSSL, etc. — those live in [`packages/kernel`](../kernel/).

**Fake metal** — `native_sim`, fast dev loop on Linux.  
**Real metal** — QEMU x86_64 multiboot/EFI, VirtualBox, hardware.

**Target:** portable orchestrator guest (`wasm32-wasip1`) + per-target host (`linux`, `zephyr`, `rump`, `unikraft`). Host probes RAM and writes `/sys/pm` once at boot; guest `pm_sys` reads it once, caches, and runs layout policy in `orchestrator/boot` + `pm_mem`.

---

## Documentation

| Doc | What |
|-----|------|
| [docs/LAYERS.md](docs/LAYERS.md) | Layer model — host stack, guest stack, WASI |
| [docs/SOURCETREE.md](docs/SOURCETREE.md) | Target repo layout (`include/` / `host/` / `guest/`) |
| [docs/PLATFORM.md](docs/PLATFORM.md) | Contract / policy / port split |
| [docs/MEMORY_MODEL.md](docs/MEMORY_MODEL.md) | RAM probes, slots, arena, `/sys/pm` handoff |
| [docs/MEMORY_BASELINE.md](docs/MEMORY_BASELINE.md) | Fake vs real metal numbers today |

**Symmetry rule:** `include/pymergetic/metal/<mod>.h` + `host/<plat>/pymergetic/metal/<mod>.c` + `guest/pymergetic/metal/<mod>.c`. `port/` is host-only but lives inside `metal/port/`.

---

## Layout

### Today (working tree)

Implementation and Zephyr runtime live under **`backup/1st_try/`** while the package is restructured toward the target layout in [SOURCETREE.md](docs/SOURCETREE.md).

```
packages/metal/
├── docs/                   Architecture (authoritative for target design)
├── backup/1st_try/         Working Zephyr app + src + scripts + west manifest
│   ├── runtime/
│   │   ├── zephyr/         Zephyr application (fake + real metal)
│   │   └── linux/          Linux bench twin
│   ├── src/pymergetic/     metal policy + port mechanism
│   ├── include/pymergetic/ Public headers
│   ├── scripts/            build, verify, images
│   └── west-manifest/      west manifest (Zephyr v4.4.0 + tlsf)
└── README.md
```

Gitignored build outputs: `runtime/*/build/`, `runtime/zephyr/images/`, `mods/build/`, `compile_commands.json`, `.venv/`, `.cache/` — see `.gitignore`.

### Target (in progress)

```
include/pymergetic/metal/     contract headers
host/<plat>/pymergetic/       native shell + metal/ + wasi/ + pm_host.c
guest/pymergetic/metal/       portable wasm32-wasip1 orchestrator
```

`runtime/<plat>/` → `host/<plat>/`. Details: [SOURCETREE.md](docs/SOURCETREE.md).

---

## First-time setup

From the working tree:

```bash
cd packages/metal/backup/1st_try
west update
python3 -m venv .venv
source .venv/bin/activate
pip install west
pip install -r external/zephyr/scripts/requirements-base.txt
```

Requires [Zephyr SDK](https://github.com/zephyrproject-rtos/sdk-ng) on `PATH` (or `ZEPHYR_SDK_INSTALL_DIR`).

**IDE:** open `packages/metal/backup/1st_try` as workspace root. Zephyr app: `runtime/zephyr`, board `native_sim/native/64`. Run `scripts/setup-ide.sh` for `compile_commands.json` symlink.

---

## Build & verify

All commands from `backup/1st_try/`:

```bash
source .venv/bin/activate
export ZEPHYR_BASE="$(pwd)/external/zephyr"
```

**Fake metal (native_sim):**

```bash
west build -b native_sim/native/64 runtime/zephyr \
  -p --build-dir runtime/zephyr/build/native_sim/native/64
timeout 8 runtime/zephyr/build/native_sim/native/64/zephyr/zephyr.exe
```

**Real metal (QEMU multiboot):**

```bash
west build -b qemu_x86_64 runtime/zephyr \
  -p --build-dir runtime/zephyr/build/qemu_x86_64
west build -d runtime/zephyr/build/qemu_x86_64 -t run
```

**Twin smoke** (native_sim + qemu):

```bash
./scripts/verify-twin-targets.sh
```

**EFI / VirtualBox images:**

```bash
./scripts/build-images.sh
# → runtime/zephyr/images/pymetal-zephyr-efi.{img,vdi}
```

**Freestanding mod `.o`:**

```bash
./scripts/build-mod.sh mods/hello
# → mods/build/hello.o
```

Expected boot output includes machine RAM probe, heap steps, and a memory layout report from `pm_metal_memory_boot()`.

---

## Configuration

Kconfig merge order:

1. Board defconfig (`external/zephyr/boards/...`)
2. `runtime/zephyr/prj.conf`
3. Board fragments in `runtime/zephyr/boards/` (RAM overlays, fake-metal blob size)
4. Extra conf via west / IDE (e.g. `qemu_x86_64_efi.conf`)

Merged config after build:

```
runtime/zephyr/build/<board>/zephyr/.config
```

---

## Scope

| Package | Responsibility |
|---------|----------------|
| **metal** (this) | Orchestrator stack, RAM policy, mod loader, host port layer |
| **kernel** | CPython 3.14, vendored C libs, language runtime |

Python port checklist: `../kernel/REQUIREMENTS_PYTHON.md`.
