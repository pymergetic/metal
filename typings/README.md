# Typings (Metal public faces)

Path == module. C/RS callees expose nested builtins; stubs are `.pyi` only.

| Import | Callee | Glue |
|--------|--------|------|
| `pymergetic.metal.util.lz4` | `src/.../util/lz4` | `glue/.../util/lz4.c` |
| `pymergetic.metal.util.size` | `src/.../util/size` | `glue/.../util/size.c` |
| `pymergetic.metal.util.fourcc` | `src/.../util/fourcc` | `glue/.../util/fourcc.c` |
| `pymergetic.metal.util.eightcc` | `src/.../util/eightcc` | `glue/.../util/eightcc.c` |
| `pymergetic.metal.util.tar` | `src/.../util/tar` (firmware) | `glue/.../util/tar.c` |
| `pymergetic.metal.auth` | `src/.../auth` | `glue/.../auth.c` |
| `pymergetic.metal.trust` | `src/.../trust` | `glue/.../trust.c` |
| `pymergetic.metal.externals` | `src/.../boot/externals` | `glue/.../externals.c` |
| `pymergetic.metal.net.ip` | `src/.../net/ip` (firmware) | `glue/.../net/ip.c` |
| `pymergetic.metal.net.wg` | `src/.../net/wg` (firmware) | `glue/.../net/wg.c` |
| `pymergetic.metal.net.ssh` | `src/.../net/ssh` (firmware) | `glue/.../net/ssh.c` |

No private `_pm_*` builtins. No `metalnet` / `ssh` aliases.

IDE: open metal with [`../pyrightconfig.json`](../pyrightconfig.json) (`stubPath: typings`),
or set workspace `python.analysis.stubPath` → `extmod/metal/typings`.
Restart Pylance / clangd after path moves.
