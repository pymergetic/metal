# NuttX target

Full Metal host bind next to `src/linux/` / `src/zephyr/`. WAMR already ships
`external/wamr/core/shared/platform/nuttx/` (POSIX file + real `pthread`/`sem`),
so this port stays closer to linux than to zephyr — see `docs/LAYERS.md`.

Lite note on whether common code must move: [`BASE_TOUCH.md`](BASE_TOUCH.md).

## Layout

```
src/nuttx/
  main.c                         # argv → app_run_scripted (same CLI shape as linux)
  CMakeLists.txt                 # NuttX cmake app (primary build)
  Kconfig, Make.defs, Makefile
  configs/sim-metal.config       # fragment on top of sim:nsh
  pymergetic/metal/
    port/{platform,lock,worker,pipe}.c
    memory/{ram,kheap,bytecode}.c
    mount/{tmpfs,device}.c
    wasi/{file,posix_file_real}.c   # Metal os_* wrap (parity with linux)
```

## Bring-up (sim)

```bash
./scripts/setup nuttx          # external/nuttx, nuttx-apps, WAMR, app symlink
./scripts/build nuttx sim      # build/nuttx/sim/nuttx
./scripts/verify nuttx sim     # nsh scripted smoke (hostfs + host curl)
```

Manual nsh (watch `CONFIG_LINE_MAX` — long lines truncate; prefer the verify script):

```text
mount -t hostfs -o fs=/path/to/vfs /v
pm_metal --vfs-root=/v /t0.wasm
# (omit --memory/--bytecode-memory → server layout, same as linux/zephyr)
```

Use **copies** of `.wasm` files on the host dir, not symlinks — hostfs
reports host symlinks via `stat`, then nsh `ls` calls `readlink`, but
hostfs has no `readlink` op → `EINVAL` (22). Harmless for load/open.

`sim-metal.config` enables `CONFIG_DEV_URANDOM` — WAMR picks WASI fds with
`/dev/urandom`; without it, guest `open()` fails with ENOENT after a
successful host open.

## Bring-up (qemu-intel64)

Real kernel under QEMU (KVM when `/dev/kvm` is usable). Guest mods/stdlib
come from embedded lz4 packages — no hostfs.

```bash
./scripts/setup nuttx
./scripts/build nuttx qemu     # build/nuttx/qemu/nuttx
./scripts/verify nuttx qemu    # serial nsh smoke (skips t31 for now)
```

`qemu-metal.config` bumps `CONFIG_RAM_SIZE` for python.wasm malloc pools,
uses readline (not CLE), `USEC_PER_TICK=10000`, and a larger 16550 RX
buffer so scripted nsh lines are not dropped. `PM_NUTTX_QEMU_ACCEL=auto|kvm|tcg`
selects accel (same idea as Zephyr).

**Deferred on qemu:** `t31_net_util` (HTTPS) — needs NET + mbedTLS (or similar);
sim uses host libcurl only.

## Parity intent

| Surface | Status |
|---------|--------|
| port + memory binds | implemented |
| tmpfs (`/tmp` mkdtemp) | implemented |
| WASI mount table + virtual `/proc` | implemented (linux-shaped wrap) |
| wasi-threads / process workers | implemented (portable try_join) |
| verify nuttx sim | scripted smoke + t31 (host curl) |
| verify nuttx qemu | scripted smoke minus t31; embedded packages |

## Why not blocked on Zephyr

Zephyr needs a custom `os_*` file shim because it is not POSIX `*at()`. NuttX
reuses WAMR `posix_file.c` like linux; Metal’s wrap is for virtual proc /
live remount parity, and also works around a NuttX libc `*at` path-join bug
(`F_GETPATH` + `strlcat` omits `/` → `/v`+`README` becomes `/vREADME`).
