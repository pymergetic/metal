# iface — language packs + symbol table

Short reference. Design + rationale: `docs/DOC_IFACE_PLAN.md` Part II.
Sibling: Part I callable-docs catalog (`pymergetic.metal.doc`) — `iface`
never duplicates that text, only points at it via `doc_key`.

## What it is

Two independent, read-only artifacts a guest (or the shell) can browse at
runtime:

1. **Language packs** — named, versioned `lz4(ustar)` archives inflated once
   at boot. **Pack name** is `<kind>@<base>` (kind first):

   | Kind | Pack examples | Content |
   |------|---------------|---------|
   | `h` | `h@metal.guest`, `h@mod.t8_multimod_lib` | Public `.h` |
   | `c` | `c@metal.guest` | Full C/asm/h rebuild tree (opt-in) |
   | `pyi` | `pyi@metal.guest` | `typings/**/*.pyi` |
   | `py` | `py@metal.guest`, `py@metal.stdlib` | Product / Easy stdlib `.py` |
   | `meta` | `meta@metal.guest`, `meta@metal.guest.docs` | LICENSE/README/`docs/*.md` |
   | `sysroot` | (reserved) | — |

   Kconfig: `config/metal/util/Kconfig.iface` (see [`KCONFIG.md`](KCONFIG.md)).

2. **Symbol table** — one row per `NativeSymbol` harvested at build time
   (`{module, name, sig, class_, doc_key}`).

## Python layout (import identity)

Easy stdlib imports from loose `/mods/py/stdlib` (staged + optionally
materialized from `py@metal.stdlib` at `py_init`). Product ESP `/mods`
and ustar paths inside `py@metal.guest` match:

| Host | ESP / pack | `import` |
|------|------------|----------|
| `mods/httpd/*.py` | `httpd/…` | `httpd` |
| `mods/api/api/**` | `api/…` | `api` |
| `mods/api/templates/**` | `templates/…` | `templates` |
| `external/…/microdot` | `microdot/…` | `microdot` |
| `external/utemplate` | `utemplate/…` | `utemplate` |

Staging: `scripts/lib/stage-py-trees.sh` (shared by ESP stage + embed).
`.pyi` packs mirror `typings/` and are **not** on MicroPython `sys.path`.

## Shell / Python API

```text
iface                                  # list packages
iface ls h@metal.guest
iface cat h@metal.guest fs/fs.h
iface sym pymergetic.metal.fs pm_metal_fs_open_async
```

```python
import pymergetic.metal.iface as iface
iface.info()  # {name: {kind, version, ...}, ...}
iface.read("py@metal.guest", "httpd/__init__.py")
iface.read("pyi@metal.guest", "pymergetic/metal/fs.pyi")
iface.read("h@metal.guest", "fs/fs.h")
```

## Kconfig (menuconfig → util → iface)

- `PM_METAL_IFACE_EMBED_C_HEADERS` (default y) — `h@metal.guest`, meta/docs, t8
- `PM_METAL_IFACE_EMBED_C_IMPL` (default n) — `c@metal.guest`
- `PM_METAL_IFACE_EMBED_PYTHON_HEADERS` (default y) — `pyi@metal.guest`
- `PM_METAL_IFACE_EMBED_PYTHON_IMPL` (default y) — `py@metal.guest`
- `py@metal.stdlib` — always baked when C headers are on (mandatory for
  MicroPython); also staged loose to `/mods/py/stdlib` for import

Bake script: `scripts/build.d/port/efi/embed-iface.sh` →
`iface_metal_guest_embed.inc.c`.

## External apps (ESP sidecars)

`mods/apps/<name>/iface.list` — five fields per line:

```text
name kind version uncompressed_len blob_filename
```

`name` uses `<kind>@<base>`; `kind` must match:
`h` / `c` / `pyi` / `py` / `meta` / `sysroot`.

Example:

```text
h@mod.doom h <ver> <ulen> mod.doom.tar.lz4
c@mod.doom c <ver> <ulen> mod.doom.c.tar.lz4
meta@mod.doom meta <ver> <ulen> mod.doom.meta.tar.lz4
meta@mod.doom.docs meta <ver> <ulen> mod.doom.docs.tar.lz4
py@mod.doom py <ver> <ulen> mod.doom.py.tar.lz4
```

Loose `.py` for import goes under `/mods/<pkg>/`. `autoload.py` runs on
boot scan and on `mod.load` via `pm_metal_py_autoload_for_mod`.

## HTTP UI

Mount `py:httpd` + loose `/mods/httpd` / `/mods/api`.

| Path | What |
|------|------|
| `/iface`, `/iface/pkg/...` | Pack browse; `/iface` groups by module (collapsible), packs ordered h/c/pyi/py/meta (`/iface/pkg/h@metal.guest`) |
| `/api/iface*` | JSON |

Live smoke: `scripts/verify.d/port/efi/doc-iface-smoke.sh`.
