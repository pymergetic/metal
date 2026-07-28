# Kconfig — Metal build + module budgets

Metal uses a small [Kconfiglib](https://github.com/ulfalizer/Kconfiglib)
tree under [`config/`](../config/) so compile-time budgets, iface embed
toggles, and default build/upload actions live in one place.

Menu layout mirrors `pymergetic.metal` modules:

```text
Build                          # efi/bios/mod + bios x86_64/i386 + post checkboxes
pymergetic.metal
  net
    io                         # PM_METAL_IO_WIRE_MAX
    asgi                       # ASGI_* budgets
    tls                        # PM_METAL_TLS_WIRE_MAX
  py                           # heap blob sizes
  util
    iface                      # embed h / c / pyi / py packs
```

## Commands

| Command | Effect |
|---------|--------|
| `./scripts/menuconfig` | TUI; writes `config/.config` then runs `confgen` |
| `./scripts/oldconfig` | Refresh `.config` after Kconfig edits |
| `./scripts/confgen` | Emit `build/autoconf.h` + `build/config.sh` |

`scripts/build` (and EFI/BIOS build scripts) call `confgen` automatically
when needed. Commit `config/defconfig` only; keep `config/.config` local
(gitignored).

## Override order

For a single `scripts/build` invocation (highest wins):

1. `config/defconfig` / `config/.config` (via `build/config.sh`)
2. Env: `PM_METAL_BUILD_TARGET`, `PM_METAL_BIOS_ARCH`, `PM_METAL_POST_ACTION`
3. Argv: `scripts/build efi`, `scripts/build bios i386`, …

Build targets in menuconfig are **independent checkboxes** (`efi` / `bios` /
`mod`) — any combination. BIOS arch is two more checkboxes (`x86_64` /
`i386`); enabling both builds both metal.elf variants. With no argv,
`scripts/build` builds every enabled target (order: mod, efi, then each
selected BIOS arch). `scripts/build all` means mod + efi + bios for every
enabled BIOS arch. `scripts/build bios i386` still forces a single arch.

Post-actions are also independent checkboxes; menuconfig only offers them
when the matching targets are on: `upload-efi` needs `efi`, `upload-pxe`
needs `bios` plus at least one BIOS arch, `run` needs any build target.
`scripts/build` enforces the same gates against **what this invocation
built** (argv/env cannot upload-efi after a bios-only build, etc.).
Enabled posts run in upload-efi → upload-pxe → run order. Set
`PM_METAL_POST_ACTION=none` to skip for one call (multi-step child builds
do this automatically so posts run once at the top level).

## Memory size units

Menu prompts for memory budgets use **KiB** or **MiB** (fixed per symbol;
no unit switch). Counts and path lengths stay unitless / bytes.
`config/.config` stores those human values (`*_KIB` / `*_MIB`).
`scripts/confgen` also emits the stable byte macros C already uses
(`CONFIG_PM_METAL_ASGI_IO_MAX`, `CONFIG_PM_METAL_PY_BLOB_BYTES`, …).

If an old local `.config` still has byte-sized ints under the previous
symbol names, copy from `config/defconfig` or re-run `./scripts/menuconfig`.

## C side

`build/autoconf.h` defines `CONFIG_PM_METAL_*` (human unit symbols plus
byte aliases for size budgets). Public headers keep stable names
(`PM_METAL_ASGI_IO_MAX`, `ASGI_HDR_MAX`, …) as aliases of the byte
macros. EFI/BIOS compile with `-include autoconf.h` (and `-I build`).

The runtime [`pymergetic.metal.mem.limit`](../include/pymergetic/metal/runtime/mem/limit.h)
catalog registers those same macros — `/limits` and `/api/limits` stay the
in-guest face of the build config.

## Iface packs

| Symbol | Default | Pack |
|--------|---------|------|
| `PM_METAL_IFACE_EMBED_C_HEADERS` | y | `h@metal.guest`, meta packs, `h@mod.t8_multimod_lib` |
| `PM_METAL_IFACE_EMBED_C_IMPL` | n | `c@metal.guest`, depends on C headers |
| `PM_METAL_IFACE_EMBED_PYTHON_HEADERS` | y | `pyi@metal.guest` |
| `PM_METAL_IFACE_EMBED_PYTHON_IMPL` | y | `py@metal.guest` |
| (always with C headers) | — | `py@metal.stdlib` mandatory for µPy |

Sources (when enabled) are the full metal rebuild tree — shared + EFI/BIOS
port `.c`/`.S`/`.s`/`.h` plus `include/pymergetic/metal`, excluding
generated `*.inc.c`. See [`IFACE.md`](IFACE.md).
