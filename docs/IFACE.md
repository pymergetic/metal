# iface — header packs + symbol table

Short reference. Design + rationale: `docs/DOC_IFACE_PLAN.md` Part II.
Sibling: `docs/DOC_IFACE_PLAN.md` Part I / this package's own callable-docs
catalog (`pymergetic.metal.doc`, `help`/`pmcmd.<name>.__doc__`) — `iface`
never duplicates that text, only points at it via `doc_key`.

## What it is

Two independent, read-only artifacts a guest (or the shell) can browse at
runtime, without a second copy of anything that already exists on disk or
in a build-time table:

1. **Header packs** — a named, versioned `lz4(ustar)` archive of `.h`
   files, inflated once at boot and kept in host memory. `metal.guest`
   (baked from `metal.h`'s own `#include` list + `wasi.h` + `version.h` +
   `guest/mod/*.h`) is the v1 seed; `mod.t8_multimod_lib` is a second,
   much smaller pack proving a *mod* can publish its own headers too, not
   just the kernel.
2. **Symbol table** — one row per `NativeSymbol` entry harvested at build
   time from every `wasm_runtime_register_natives(module, syms, n)` call
   site under `src/pymergetic/metal/**` — `{module, name, sig, class_,
   doc_key}`. `sig` is the exact WAMR signature string (`"($i)i"`, …), not
   a second hand-maintained ABI description. `doc_key`, when set, is a
   pointer `"<kind>:<key>"` into the Part I doc catalog (see
   `scripts/iface_doc_keys.txt`) — never new prose.

## Why lz4(ustar), not a raw sysroot

A `wasi.sysroot` package kind exists in the type (`pm_metal_iface_pkg_kind_t`)
for a possible future "ship a matching wasi-sdk sysroot" pack, but v1 only
ever registers `PM_METAL_IFACE_PKG_HEADERS` packs — small enough that lz4
easily beats the cost of carrying a raw tar in the image, and consistent
with every other embedded blob in this tree (`mods/py/stdlib.zip`, guest
`.wasm` packages) already using the same `util/lz4` + `util/tar` pair.

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

Header packs are baked at build time by
`scripts/build.d/port/efi/embed-iface.sh` (wired into both `efi` and
`bios` `default.sh`, right after `embed-stdlib.sh`) into
`src/pymergetic/metal/util/iface_metal_guest_embed.inc.c`, which
`iface_embed_install.c` compiles and whose `pm_metal_iface_embed_install()`
is called once from `pm_metal_wasm_init()` (`guest/wasm/wasm.c`) before any
`iface`/`pymergetic.metal.iface` access. To publish a mod's own headers as
a third pack, add another `pack_one` call to that script pointing at the
mod's `include/` directory (mirroring the existing `mod.t8_multimod_lib`
call) — do not hand-roll a second lz4/ustar pipeline; reuse the one
`pack_one` helper.

## Adding a `doc_key` to a native symbol

`scripts/gen_iface_syms.py` merges `scripts/iface_doc_keys.txt` (one
`<wasi-module> <c-symbol> <doc_key>` line per override) into the
build-scraped table — see that file's own header comment for the exact
format. Only add a line for a symbol that already has a real Part I doc
entry (shell/py/mod); `iface` is a pointer, not a place to originate text.
