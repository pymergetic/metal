# pymergetic-metal

Pymergetic-metal: native **runtime** per target that runs **wasm** mods (`wasm32-wasip1`).

**Not here:** CPython, zlib, OpenSSL, etc. — those live in [`packages/kernel`](../kernel/).

**Runtime** — long-lived native binary per target: dynamic load/run/unload via WAMR.  
**Mods** — `.wasm` files loaded and executed through the wasm interface.

---

## Documentation

| Doc | What |
|-----|------|
| [docs/LAYERS.md](docs/LAYERS.md) | Base layer model — hardware/OS up to the wasm interface |
| [docs/WASI.md](docs/WASI.md) | WASI preview1 syscalls, host requirements, tiers |
| [docs/RUNTIME.md](docs/RUNTIME.md) | Process model — long-lived dynamic loader |
| [docs/MEMORY.md](docs/MEMORY.md) | Host pools vs guest linear memory / mounts — platform comparison + lockstep knobs |
| [docs/SOURCETREE.md](docs/SOURCETREE.md) | Folder layout (`include/` / `src/`) |
| [docs/MOUNT.md](docs/MOUNT.md) | Mount system — table, fstab, tmpfs, populate, guest `mount()`/`umount()`, virtual `/proc`, live remount (**linux feature-complete** through Phase 6c; Zephyr deferred) |

---

## Layout

```
packages/metal/
├── include/pymergetic/metal/   mod-facing (metal.h, util/{size,arena,log,lz4,tar,crypto,ntp,http}.h)
├── src/
│   ├── common/pymergetic/metal/  cross-target runtime + contracts
│   ├── linux/                    OS bind — builds pm-linux-runtime
│   ├── zephyr/                   OS bind — stub bring-up (native_sim / qemu_x86_64)
│   ├── nuttx/                    [stub — see docs/LAYERS.md, cheaper bring-up than zephyr]
│   ├── rump/                     [stub]
│   └── unikraft/                 [stub]
├── mods/                       t0..t20 — test .wasm guests (wasi-sdk, wasm32-wasip1)
├── apps/                        [empty — later]
├── scripts/                    build-linux.sh, build-mod.sh, verify-*.sh, setup-*.sh
├── patches/{wamr,microtar,…}/  tracked diffs against external/* — see docs/SOURCETREE.md § Vendoring
├── docs/
├── external/                    gitignored — vendored deps, reproduced by scripts/setup-*.sh
└── west-manifest/
```

See [docs/SOURCETREE.md](docs/SOURCETREE.md).

---

## Quickstart (linux)

```bash
scripts/setup-wamr.sh      # once — vendors + patches external/wamr
scripts/setup-lz4.sh       # once — vendors external/lz4 (util/lz4.h's backing lib)
scripts/setup-microtar.sh  # once — vendors + patches external/microtar (util/tar.h's backing lib)
scripts/setup-net.sh       # once — Monocypher + mbedTLS + nghttp2 + curl (util/{crypto,ntp,http})
scripts/setup-ide.sh       # once — compile_commands.json + .clangd for this checkout
scripts/verify-linux.sh    # main Linux verify (scripted + process/socket smoke; peer of verify-zephyr-*.sh)
scripts/verify-linux-net.sh  # util/{crypto,ntp,http} smoke (needs network)
```

Also: `scripts/verify-linux-threads.sh` (TSan). Focused helpers (`verify-linux-process.sh`, tmpfs/proc/…) are still callable alone; the main script chains the process/socket half.

---

## Visibility

- Public metal API: always visible in headers (mod compiles need no extra define).
- Kernel-only API: `-DPM_METAL_BUILD_KERNEL` (`metal/build.h`).
- Privileged mod: same wasm format, compiled with `-DPM_METAL_BUILD_KERNEL`.

---

## Build status

**Linux runtime — green.** Long-lived `pm-linux-runtime` binary: `init` → dynamic `load`/`run`/`unload` (many mods per process, no restart) → `shutdown`, over a real mount table (a Linux-like `hostdir` root mount, `/etc/fstab`, `--mount=` CLI sugar, `tmpfs` fstype (`/dev/shm`-backed, named sources), boot-time ustar [+ lz4] populate against guest `/`, privileged guest `mount()`/`umount()` with same-process visibility, virtual `/proc` — see `docs/MOUNT.md`; single `vfs_root` is now just its "root, no other mounts" special case, `--vfs-root=` kept as a deprecated alias). Landed on top of that: WASI preview1 tier T1 (file I/O under the resolved mount table), multi-module (`.wasm` importing another `.wasm`), decoupled processes (`spawn`/`wait`/`kill`, host pipes between mods), WASI preview1 sockets (loopback TCP proven across two spawned processes), and concurrent `load`/`run`/`unload` across handles/threads proven race-free under ThreadSanitizer (including a real WAMR thread-manager race fixed via a tracked vendor patch, `patches/wamr/`).

**Zephyr** — scaffolded (`src/zephyr/`, CMake/Kconfig/`prj.conf`), not yet brought up. **NuttX / Rump / Unikraft** — stubs only (NuttX's own stub notes why it should be cheaper to bring up than Zephyr — see `src/nuttx/README.md`).

Mods build against `wasm32-wasip1-threads` by default (`scripts/build-mod.sh`); guest `pthread_create()` is covered by `mods/t23_pthread` in `scripts/verify-linux.sh`.

Detail + remaining steps: [docs/RUNTIME.md § Bring-up plan](docs/RUNTIME.md#bring-up-plan).

---

## Scope

| Package | Responsibility |
|---------|----------------|
| **metal** (this) | Runtime (upper+lower) + mod loading |
| **kernel** | CPython 3.14, vendored C libs, language runtime |
