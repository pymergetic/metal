# Metal filetree (product-relevant)

Laws: wasmmod matrix · callee one lang · kernel = code container · Inspect = own mod · Microdot = kernel CORE Py (not a wasm pack).

**Path == module (every lang):** `include|src|glue|typings`/…/`pymergetic/metal/<path>` ↔ import `pymergetic.metal.<path>`.
C/RS callees → nested builtins (**glue**) + `.pyi`. No private `_pm_*` fucknames.
**No frozen reexports / empty `__init__.py` for C/RS** — if it needs a Py face, it’s glue, not a weird little `.py`.

Spines: [ARCH.md](ARCH.md) — **arch · port · wamr_host · wasmmod**.
Ledgers: [../include/SYMBOLS.md](../include/SYMBOLS.md) (symbol spelling) ·
[MODULE_MATRIX.md](MODULE_MATRIX.md) (export % · async compliance · maintain hints).

## Checkout tree

```text
extmod/metal/
  include/pymergetic/metal/      # public C ABI (path == module)
    SYMBOLS.md                   # C ↔ RS ↔ Py ledger
    util/{lz4,tar,size,…}/__init__.h
    auth/ trust/ net/{ip,wg,ssh}/ …

  src/pymergetic/metal/          # ONE callee per path (.c | .rs | .py)
    util/lz4/  auth/  trust/  net/…
    arch/  microdot/  inspect/   # CORE Py seats stay here

  glue/pymergetic/metal/         # thin µPy nest (mirrors include/)
  typings/pymergetic/metal/      # .pyi only for C/RS faces

  port/                          # µPy image adaptation ONLY
    boot/  live/  bringup/  upy/ # live/ = firmware LIVE proofs + QEMU helpers
    boards/  hal/  webassembly/  unix/
    Makefile  manifest*.py

  deploy/                        # PXE ops (bootserver + upload-bootserver)

  docs/ARCH.md  docs/SOURCETREE.md  docs/MODULE_MATRIX.md
```

## How Microdot gets in

| Step | What |
|------|------|
| Source of truth | `src/pymergetic/metal/net/microdot/*.py` (path == `net.microdot`) |
| Mod / VFS | `/mods/pymergetic.metal/net/microdot/…` |
| Import | `from pymergetic.metal.net.microdot import …` |
| C/RS | into-Py bridges via `pm_upy_*` (W4) — do not reimplement Microdot in C |
| Not | top-level `microdot` reexport · wasm pack · under `inspect` |

## VFS view (runtime)

```text
/mods/pymergetic.metal/
  net/asgi/…            ← C floor; Py ASGI apps mount here later
  net/microdot/…        ← frozen CORE Py muscle (path == module)
  arch/…                ← seat modules (same pack face)
  httpd.json            ← only here (host that runs ASGI)

/mods/pymergetic.metal.inspect/
  app.py
  adapter_microdot.py
  www/inspect/          ← UI files only (no httpd.json)
```

```json
{
  "static": [
    {
      "url": "/inspect",
      "root": "/mods/pymergetic.metal.inspect/www/inspect",
      "theme": "metal"
    }
  ]
}
```

`httpd.json` lives once on the **ASGI host** mod (`pymergetic.metal`). Inspect mod ships assets + app; it does not own server config.

| Piece | Mod | Role |
|-------|-----|------|
| Microdot framework `.py` | `pymergetic.metal` | CORE callee |
| ASGI / async / net | `pymergetic.metal` | host runtime |
| Arch seats (CDN packs) | `pymergetic.metal.arch.{x86,x86_64,wasm}` | role=`arch` (copper) |
| Unix host seats (CDN) | `pymergetic.metal.unix.{x86,x86_64}` | role=`host` (blue); curl-and-run ELF |
| Freestanding x86_64 (CDN) | `pymergetic.metal.arch.x86_64` | `.elf` BIOS trampoline + `.efi` UEFI |
| Freestanding x86 (CDN) | `pymergetic.metal.arch.x86` | i686 Multiboot `.elf` + `BOOTIA32.efi` |
| Browser seat (CDN) | `pymergetic.metal.arch.wasm` | `.mjs` + `.wasm` — CDN UI `mp` pill |
| Boot UX | `port/boot/boot.c` + `src/.../boot/tree.c` | live tree → ready → rainbow MetalPython |
| WAMR-on-box | `wamr_host/` | firmware hosts guest `.wasm` |
| stubs · Inspect app · www | `pymergetic.metal.inspect` | role=`kernel` (green) |
| CDN engine pack | `pymergetic.wasmmod` | role=`engine` (purple); `mpwm` pill |
| CDN engine `mp` | lead `arch.wasm` artifacts | metal webassembly build; static/repl fallback |
| CDN engine `mpwm` | wasmmod only | no metal arch |
| CDN FastAPI adapter | outside | same contract; theme=cdn |
