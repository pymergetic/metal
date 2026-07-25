# Agent notes for this repo

Read [`docs/SOURCETREE.md`](docs/SOURCETREE.md) before touching anything under
`src/` or `include/` — it defines the tree layout and the C dialect rule.

## The one rule you must not relitigate

`src/pymergetic/metal/**` is uniform ISO C / `stdint.h` — **one spelling per
type, everywhere**: `uint32_t` not `UINT32`, `void` not `VOID`, `static` not
`STATIC`, `bool` not `BOOLEAN`. EDK2 headers/types (`Uefi.h`, `Library/*.h`,
`UINT32`, ...) are only allowed physically under `src/efi/**` / `src/bios/**`
— never by filename convention (no `*_port.c` exception), never "just this
once", never behind a compat `#define`.

This is strict because the project owner is autistic and a type that can
appear under two different spellings (even when typedef-identical) costs
real, avoidable reading effort for their pattern matching. Treat this as a
hard requirement, not a style preference to debate. See `.cursor/rules/metal-c-dialect.mdc`
and `docs/SOURCETREE.md` § "C dialect" for the full rule, the containment
pattern for genuine EDK2 primitives (`*_port.c` split), and the verification
`grep`.

Before finishing any change touching `src/pymergetic/metal/**`, run:

```
grep -rlE '#include\s*<(Uefi\.h|Library/|Protocol/|IndustryStandard/)' src/pymergetic/metal --include=*.c --include=*.h
```

It must return nothing.

## Cursor Cloud specific instructions

Active target is **`efi`** (freestanding UEFI `metal.efi`, booted under QEMU+OVMF).
The product is a thin bare-metal runtime that runs `wasm32` guest mods; the
shell/UI is the interactive surface. Standard commands live in `scripts/`
(`setup`, `build`, `verify`, `run`) and `docs/EFI.md` — reference those, not
duplicated command lists here.

Dependency vendoring (`external/**`, `.tools/**`) is handled by the startup
update script; system packages (qemu, ovmf, iasl/acpica-tools, uuid-runtime,
clang-format, socat, `qemu-system-gui`) are baked into the VM snapshot. The
notes below are the non-obvious caveats — read them before building/running.

- **Hardcoded developer path in `src/efi/MetalPkg/Metal.inf` (build blocker).**
  Its `[BuildOptions] GCC:*_*_*_CC_FLAGS` line has absolute `-I` paths under
  `/home/ladmin/Devel/os-sdk/packages/metal/...` (notably the
  `build/micropython_embed` root that resolves `py/compile.h`). The build only
  regenerates the `[Sources]` `BEGIN/END_MICROPYTHON` block, not these flags, so
  those absolute paths must resolve. This repo is checked out at `/workspace`,
  so a symlink makes them valid (do NOT edit `Metal.inf`):
  `sudo mkdir -p /home/ladmin/Devel/os-sdk/packages && sudo ln -sfn /workspace /home/ladmin/Devel/os-sdk/packages/metal`.
  This symlink persists in the VM snapshot; recreate it if `./scripts/build efi`
  fails with `py/compile.h: No such file or directory`.
- **`external/tlsf` has no setup script.** `scripts/setup` vendors everything
  except TLSF (needed by `runtime/mem/tlsf_edk2.c` and the host regressions).
  It is vendored from `https://github.com/mattconte/tlsf` into `external/tlsf`
  (done by the update script); clone it there if missing.
- **No KVM in the VM → QEMU runs under TCG (`accel=kvm:tcg` falls back).**
  Everything works but is ~10x slower. `./scripts/verify efi` has a 180s
  deadline that TCG can bump against; the boot + all guest proofs still run.
- **`./scripts/verify efi` currently fails only at the `async_audio` proof**
  (`metal-async: audio fail`). Under headless QEMU 8.2 + TCG with
  `-audiodev none`, the virtio-snd buffer-drain `await` in `t_async_audio` does
  not complete, so the all-or-nothing suite never prints `metal-test: ok`. Boot,
  ExitBootServices, gfx/ui/wasm/shell, `metal-blk: lba0 ok`, and every other
  async proof (sleep/fs/fs-fd/time/blk/net/http/tftp) pass. Treat this as a
  known emulation caveat, not a regression.
- **Interactive run / manual demo:** `export DISPLAY=:1` then
  `./scripts/run efi --gtk` opens the QEMU window on the desktop VNC server
  (needs `qemu-system-gui`; without it QEMU errors "Display 'gtk' is not
  available"). Boot to `metal:~$` takes ~40s under TCG. In the shell, `help`
  lists commands; run an embedded wasm mod with e.g. `run async_sleep`
  (prints `metal-async: sleep ok`). `run efi` uses `-serial stdio` so serial
  logs appear in the launching terminal.
- **`./scripts/build mod`** (full guest test suite) fails on the pre-existing
  `t11_socket_client` (`sched_yield` implicit declaration) — unrelated to the
  EFI product, which embeds its own mod subset via
  `scripts/build.d/port/efi/embed-mods.sh` (run automatically by `build efi`).
- **`./scripts/verify format`** silently passes if `clang-format` is absent
  (the wrapper treats "command not found" as "needs formatting" for every
  file). `clang-format` (v18) must be installed for the check to be meaningful;
  it reports some pre-existing drift in `mods/tests/**` and a few `src/**`
  files.
- The build regenerates `src/pymergetic/metal/guest/wasm/embed_mods.inc.c`
  (embedded guest bytes) — it shows up as a tracked modification after
  `build efi`; do not commit it.
