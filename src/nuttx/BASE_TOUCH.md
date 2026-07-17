# NuttX vs existing code — lite touch list

Assumption: linux + zephyr already exercised the common contracts, so a NuttX
port should stay under `src/nuttx/` and only rarely need `src/common/` changes.

## Done without common changes

- All `/* impl: bind */` symbols implemented under `src/nuttx/pymergetic/metal/`
- WASI wrap copied/adapted from linux (virtual `/proc` + live remount) — plat-local
- Worker `try_join` without `pthread_tryjoin_np` (NuttX gap) — plat-local
- tmpfs under `/tmp` + recursive teardown (no `/dev/shm`, no `nftw`) — plat-local
- `PM_METAL_PORT_TARGET_NUTTX` already existed in `port/platform.h`

## Residual / optional common touches

| Item | Need? | Notes |
|------|-------|-------|
| `port/{platform,lock,worker,pipe}.h` bind comments | done (cosmetic) | Listed `src/nuttx/…` next to linux/zephyr |
| `docs/SOURCETREE.md` / `LAYERS.md` | done | Stub → real tree + nuttx column |
| Shared wasi/file between linux & nuttx | **no** (for now) | Duplication matches zephyr’s private wasi/; extract later only if drift hurts |
| New port APIs | unlikely | NuttX is POSIX/`pthread` like linux |
| Runtime / mount table changes | unlikely | hostdir + tmpfs + proc already designed |

## Do not touch while Zephyr gap-scan lands

- `src/zephyr/**`
- Common mount/WASI redesigns driven by Zephyr’s `wasi/file.c` cliff — NuttX does not need that shim shape

If a future verify on sim exposes a real common bug (not a bind bug), fix it in
`src/common/` with a linux regression run — that should stay rare.
