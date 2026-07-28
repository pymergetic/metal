# iface — header packs + symbol table

Short reference. Design + rationale: `docs/DOC_IFACE_PLAN.md` Part II.
Sibling: `docs/DOC_IFACE_PLAN.md` Part I / this package's own callable-docs
catalog (`pymergetic.metal.doc`, `help`/`pmcmd.<name>.__doc__`) — `iface`
never duplicates that text, only points at it via `doc_key`.

## What it is

Two independent, read-only artifacts a guest (or the shell) can browse at
runtime, without a second copy of anything that already exists on disk or
in a build-time table:

1. **Header / sources / meta packs** — named, versioned `lz4(ustar)` archives
   inflated once at boot and kept in host memory. Kinds:
   `PM_METAL_IFACE_PKG_HEADERS`, optional `PM_METAL_IFACE_PKG_SOURCES`, and
   `PM_METAL_IFACE_PKG_META` (Kconfig: `config/metal/util/Kconfig.iface`,
   see [`KCONFIG.md`](KCONFIG.md)).
   `metal.guest` (headers from `metal.h`'s own `#include` list + `wasi.h` +
   `version.h` + `guest/mod/*.h`) is the v1 seed; with headers also come
   `metal.guest.meta` (`LICENSE` + `README.md`) and `metal.guest.docs`
   (`docs/*.md`). When sources embed is on, `metal.guest.sources` ships the
   full rebuild tree (`.c` / `.S` / `.s` / `.h` under `src/pymergetic/metal`,
   `src/{efi,bios}/pymergetic/metal`, and `include/pymergetic/metal`; no
   generated `*.inc.c`) for later JIT / in-guest rebuild.
   `mod.t8_multimod_lib` is a second, much smaller header pack proving a
   *mod* can publish its own headers too.
2. **Symbol table** — one row per `NativeSymbol` entry harvested at build
   time from every `wasm_runtime_register_natives(module, syms, n)` call
   site under `src/pymergetic/metal/**` — `{module, name, sig, class_,
   doc_key}`. `sig` is the exact WAMR signature string (`"($i)i"`, …), not
   a second hand-maintained ABI description. `doc_key`, when set, is a
   pointer `"<kind>:<key>"` into the Part I doc catalog (see
   `scripts/iface_doc_keys.txt`) — never new prose.

## Why lz4(ustar), not a raw sysroot

A `wasi.sysroot` package kind exists in the type (`pm_metal_iface_pkg_kind_t`)
for a possible future "ship a matching wasi-sdk sysroot" pack. Registered
kinds today are `headers`, optional `sources`, and `meta` (LICENSE/README +
`docs/*.md`) — small enough that lz4 easily beats a raw tar in the image,
consistent with every other embedded blob in this tree (`mods/py/stdlib.zip`,
guest `.wasm` packages) already using the same `util/lz4` + `util/tar` pair.

## Shell

```text
iface                                  # list registered packages
iface ls                               # same
iface ls metal.guest                   # files inside one package
iface cat metal.guest pymergetic/metal/fs/fs.h
iface sym                              # every NativeSymbol row
iface sym pymergetic.metal.fs          # rows in one wasi module
iface sym pymergetic.metal.fs pm_metal_fs_open_async
```

## Python

```python
import pymergetic.metal.iface as iface

iface.info()                                    # {name: {kind, version, abi_hash, nfiles, blob_len}, ...}
iface.list()                                     # ["metal.guest", "mod.t8_multimod_lib"]
iface.list("metal.guest")                        # every file path inside that pack
iface.read("metal.guest", "pymergetic/metal/fs/fs.h")   # bytes
sym = iface.sym("pymergetic.metal.fs", "pm_metal_fs_open_async")
# {module, name, sig, class_, doc_key}
if sym and sym["doc_key"]:
    import pymergetic.metal.doc as doc
    doc.lookup_key(sym["doc_key"])
```

## Adding a new header pack

### In-tree (baked into metal.efi)

Packs are baked at build time by
`scripts/build.d/port/efi/embed-iface.sh` (wired into both `efi` and
`bios` `default.sh`, right after `embed-stdlib.sh`) into
`src/pymergetic/metal/util/iface_metal_guest_embed.inc.c`, which
`iface_embed_install.c` compiles and whose `pm_metal_iface_embed_install()`
is called once from `pm_metal_wasm_init()` (`guest/wasm/wasm.c`) before any
`iface`/`pymergetic.metal.iface` access. Packing uses
`scripts/lib/iface-pack.sh` (`pm_metal_iface_pack_dir`) — do not hand-roll
a second lz4/ustar pipeline.

Toggles (menuconfig → **pymergetic.metal → util → iface**):

- `PM_METAL_IFACE_EMBED_HEADERS` (default y) — bake `metal.guest`,
  `metal.guest.meta`, `metal.guest.docs`, and `mod.t8_multimod_lib`; off
  writes an empty `pm_metal_iface_embed_install()`.
- `PM_METAL_IFACE_EMBED_SOURCES` (default n, depends on headers) — also bake
  `metal.guest.sources` (full metal + port C/asm/h tree; no `*.inc.c`).

To publish an in-tree mod's own headers as another pack, add another
`pack_one` call to that script pointing at the mod's `include/` directory
(mirroring `mod.t8_multimod_lib`).

### External apps (ESP sidecars)

External apps staged via `METAL_EXT_APPS` (copied under `mods/apps/<name>/`)
can publish packs without touching Metal's embed script. At wasm init,
`pm_metal_iface_esp_install()` scans each app directory for `iface.list`.
Each non-comment line is five space-separated fields:

```text
name kind version uncompressed_len blob_filename
```

- `kind` — `headers` / `sysroot` / `sources` / `meta`
- `uncompressed_len` — ustar size before lz4 (same value embed packs pass to
  `pm_metal_iface_pkg_register`)
- `blob_filename` — basename only (no `/` or `..`), living next to
  `iface.list` under that app dir; file is lz4(ustar) built with
  `scripts/lib/iface-pack.sh`

Example (metal-doom ships these beside `doom.wasm`):

```text
mod.doom headers <ver> <ulen> mod.doom.tar.lz4
mod.doom.sources sources <ver> <ulen> mod.doom.sources.tar.lz4
mod.doom.meta meta <ver> <ulen> mod.doom.meta.tar.lz4
mod.doom.docs meta <ver> <ulen> mod.doom.docs.tar.lz4
```

Missing or bad rows are logged and skipped — boot never fails on them.
Packs show up in `iface` / HTTP `/api/iface` after boot; the guest wasm
does not need to be loaded first. At most `PM_METAL_IFACE_PKG_MAX` (64)
named packs can be registered (`iface.c`); further registers fail.

On EFI, `pm_metal_iface_esp_install()` runs in `MetalPkg/main.c` right after
`mods/apps` is preloaded (SimpleFileSystem still live). After ExitBootServices
only the RAM cache remains, so registering from `wasm.c` alone cannot discover
app directories. `wasm.c` still calls it for BIOS and as an idempotent fallback.

## Adding a `doc_key` to a native symbol

`scripts/gen_iface_syms.py` merges `scripts/iface_doc_keys.txt` (one
`<wasi-module> <c-symbol> <doc_key>` line per override) into the
build-scraped table — see that file's own header comment for the exact
format. Only add a line for a symbol that already has a real Part I doc
entry (shell/py/mod); `iface` is a pointer, not a place to originate text.

## HTTP UI (same catalogs)

When ASGI httpd is up (`mods/etc/httpd.json`, mount `py:httpd`), the
guest app `mods/httpd/httpd.py` (+ `mods/api/` routes) serves. Guest
stacks declare themselves from `mods/httpd/autoload.py` via
`pymergetic.metal.externals.register` (run once with other
`/mods/*/autoload.py` at REPL banner / after stdlib.zip ready):

| Path | What |
|------|------|
| `/`, `/docs`, `/docs/{kind}/{key}`, `/docs/key/{doc_key}` | HTML over `pymergetic.metal.doc` (utemplate + `mods/www/doc.css`) — home: [`screenshots/http-home.png`](../screenshots/http-home.png); shell filter: [`screenshots/docs-shell.png`](../screenshots/docs-shell.png) |
| `/iface`, `/iface/pkg/...`, `/iface/sym...` | HTML over `pymergetic.metal.iface` — pack list: [`screenshots/iface-packages.png`](../screenshots/iface-packages.png); syms: [`screenshots/iface-syms.png`](../screenshots/iface-syms.png) |
| `/iface/pkg/<name>/view?path=...` | Highlighted source view (C/header) — see [`screenshots/iface-source-view.png`](../screenshots/iface-source-view.png) |
| `/api/doc*`, `/api/iface*` | JSON of the same rows |

Templates live under `mods/api/templates/` (precompiled with
`mods/api/compile_templates.py`, packed by `mods/httpd/pack_zips.sh` into
`templates.zip` + `utemplate.zip` — ESP `mp_import_stat` has no DIR for
loose trees). List pages (HTML + matching `/api/*` lists) paginate with
`?page=N&limit=N` (default 50/page, max 500). HTML shows first/prev/next/last;
JSON lists are `{ "items": [...], "total", "page", "pages", "limit" }`.
Live smoke: `scripts/verify.d/port/efi/doc-iface-smoke.sh`. Package `kind`
in HTML/JSON is `headers` / `sources` / `meta` / `sysroot`.
