# pymergetic-metal

Pymergetic-metal: native **runtime** per target that runs **wasm** mods (`wasm32-wasip1`).

**Not here:** CPython, zlib, OpenSSL, etc. — those live in [`packages/kernel`](../kernel/).

**Runtime** — long-lived native binary per target: dynamic load/run/unload via WAMR.  
**Mods** — `.wasm` files loaded and executed through the wasm interface.

`backup/1st_try/` is reference only — not the codebase being built.

---

## Documentation

| Doc | What |
|-----|------|
| [docs/LAYERS.md](docs/LAYERS.md) | Base layer model — hardware/OS up to the wasm interface |
| [docs/WASI.md](docs/WASI.md) | WASI preview1 syscalls, host requirements, tiers |
| [docs/RUNTIME.md](docs/RUNTIME.md) | Process model — long-lived dynamic loader |
| [docs/SOURCETREE.md](docs/SOURCETREE.md) | Folder layout (`include/` / `src/`) |
| [docs/MOUNT.md](docs/MOUNT.md) | Mount system — table, `/etc/fstab`, `--mount=` CLI, `tmpfs` (**Phases 1–3 landed on linux**; zephyr `tmpfs`/ramdisk + guest `mount()` still design only) |

---

## Layout

```
packages/metal/
├── include/pymergetic/metal/   mod-facing (metal.h, util/{size,arena,log,lz4,tar}.h)
├── src/
│   ├── common/pymergetic/metal/  cross-target runtime + contracts
│   ├── linux/                    OS bind — builds pm-linux-runtime
│   ├── zephyr/                   OS bind — stub bring-up (native_sim / qemu_x86_64)
│   ├── nuttx/                    [stub — see docs/LAYERS.md, cheaper bring-up than zephyr]
│   ├── rump/                     [stub]
│   └── unikraft/                 [stub]
├── mods/                       t0..t15 — test .wasm guests (wasi-sdk, wasm32-wasip1)
├── apps/                        [empty — later]
├── scripts/                    build-linux.sh, build-mod.sh, verify-*.sh, setup-*.sh
├── patches/{wamr,microtar}/     tracked diffs against external/{wamr,microtar} — see docs/SOURCETREE.md § Vendoring
├── docs/
├── external/                    gitignored — vendored WAMR/Zephyr/wasi-sdk/LZ4/microtar, reproduced by scripts/setup-*.sh
├── west-manifest/
└── backup/                      old tries — not built from
```

See [docs/SOURCETREE.md](docs/SOURCETREE.md).

---

## Quickstart (linux)

```bash
scripts/setup-wamr.sh      # once — vendors + patches external/wamr
scripts/setup-lz4.sh       # once — vendors external/lz4 (util/lz4.h's backing lib)
scripts/setup-microtar.sh  # once — vendors + patches external/microtar (util/tar.h's backing lib)
scripts/setup-ide.sh       # once — compile_commands.json + .clangd for this checkout
scripts/verify-linux.sh    # build mods + runtime, then init -> load -> run -> unload -> shutdown
```

Other checks: `scripts/verify-linux-threads.sh` (ThreadSanitizer, concurrent load/run/unload), `scripts/verify-linux-process.sh` (`spawn()`/`kill()`/pipes/sockets across processes).

---

## Visibility

- Public metal API: always visible in headers (mod compiles need no extra define).
- Kernel-only API: `-DPM_METAL_BUILD_KERNEL` (`metal/build.h`).
- Privileged mod: same wasm format, compiled with `-DPM_METAL_BUILD_KERNEL`.

---

## Build status

**Linux runtime — green.** Long-lived `pm-linux-runtime` binary: `init` → dynamic `load`/`run`/`unload` (many mods per process, no restart) → `shutdown`, over a real mount table (a Linux-like `hostdir` root mount, `/etc/fstab`, `--mount=` CLI sugar, `tmpfs` fstype (`/dev/shm`-backed, named sources) — see `docs/MOUNT.md`; single `vfs_root` is now just its "root, no other mounts" special case, `--vfs-root=` kept as a deprecated alias). Landed on top of that: WASI preview1 tier T1 (file I/O under the resolved mount table), multi-module (`.wasm` importing another `.wasm`), decoupled processes (`spawn`/`wait`/`kill`, host pipes between mods), WASI preview1 sockets (loopback TCP proven across two spawned processes), and concurrent `load`/`run`/`unload` across handles/threads proven race-free under ThreadSanitizer (including a real WAMR thread-manager race fixed via a tracked vendor patch, `patches/wamr/`).

**Zephyr** — scaffolded (`src/zephyr/`, CMake/Kconfig/`prj.conf`), not yet brought up. **NuttX / Rump / Unikraft** — stubs only (NuttX's own stub notes why it should be cheaper to bring up than Zephyr — see `src/nuttx/README.md`).

Not yet exercised: guest-side `pthread_create()` (host thread-manager infra is in; no mod builds against `wasm32-wasip1-threads` yet).

Detail + remaining steps: [docs/RUNTIME.md § Bring-up plan](docs/RUNTIME.md#bring-up-plan).

---

## Scope

| Package | Responsibility |
|---------|----------------|
| **metal** (this) | Runtime (upper+lower) + mod loading |
| **kernel** | CPython 3.14, vendored C libs, language runtime |
