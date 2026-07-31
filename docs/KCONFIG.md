# Kconfig — Metal build + module budgets

Metal uses a small [Kconfiglib](https://github.com/ulfalizer/Kconfiglib)
tree so compile-time budgets and build defaults live in one place.

Live wrappers: [`.../forge/_kconfig/`](../src/pymergetic/metal/forge/_kconfig/)
via `./forge-cli config …`.

## Live

Tree: [`config/`](../config/) (nested module fragments in `.pm/Kconfig`).

| Command | Effect |
|---------|--------|
| `./forge-cli config edit` | TUI -> `config/.config` + gen |
| `./forge-cli config old` | Refresh after Kconfig edits |
| `./forge-cli config gen` | Emit `build/autoconf.h` + `config.sh` |
| `./forge-cli build [bios\|efi\|all]` | gen -> img rootfs -> mod sync -> link |
| `./forge-cli run [bios\|efi\|all]` | QEMU |
| `./forge-cli img rootfs` | Pack Stage-A blobs |

Defaults: 4 MiB root FAT, kernel `/mods` + `/src` with **human only**
source mode. Mount contract: [`definitions/fs.md`](definitions/fs.md).

Menu layout mirrors `pymergetic.metal` modules (`Build`, then nested
`pymergetic.metal.*` fragments).

## Memory size units

Menu prompts for memory budgets use **KiB** or **MiB** (fixed per symbol;
no unit switch). Counts and path lengths stay unitless / bytes.
`.config` stores those human values (`*_KIB` / `*_MIB`).
`forge config gen` / `confgen.py` also emits the stable byte macros C uses.

## C side

`build/autoconf.h` defines `CONFIG_PM_METAL_*`. EFI/BIOS compile with
`-include autoconf.h` (and `-I build`).

## Archived product Kconfig

The former package-root `config/` + `scripts/{menuconfig,oldconfig,confgen,build}`
flow is under [`_old/`](../_old/). Iface-pack toggles and µPy budgets described
historically there are not on the live forge path; see
[`_old/docs/IFACE.md`](../_old/docs/IFACE.md) if needed.
