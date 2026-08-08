# Metal filetree (proposed — product-relevant)

Laws: wasmmod matrix · callee one lang · kernel = code container · Inspect = own mod · Microdot = kernel CORE Py (not a wasm pack).

## Checkout tree

```text
extmod/metal/
  include/pymergetic/metal/
    SYMBOLS.md
    async/
      runner.h  handle.h  await.h  time.h  board_time.h  prio.h
    asgi/
      __init__.h
    microdot/
      __init__.h                 # C/RS *caller* → Py microdot under /mods/…
    inspect/                     # contract headers (CDN + guest)
      __init__.h
      endpoints.h
      py_call.h                  # C/RS caller → inspect app Py
    net/
      pump/  ip/  dhcp/  dns/  ntp/  tftp/  http/  ssh/  faces/  upy_nic/
    dev/acpi/  bus/  mem.h  console.h  …

  src/pymergetic/
    metal/                       # ── mod id: pymergetic.metal ──
      async/
        __init__.c  smp.c
      asgi/
        __init__.c
      net/
        pump/  ip/  dhcp/  dns/  ntp/  tftp/  http/  ssh/  faces/ …
      dev/acpi/
        __init__.c
      microdot/                  # ── CORE: upstream Microdot .py ──
        __init__.py              #   vendored / tracked sources
        microdot.py              #   (exact upstream layout as needed)
        websocket.py             #   …
        # NOT a wasm pack; freeze or import from VFS like other Py callees
      httpd.json                 # ONE host config (ASGI static mounts → VFS)
      # … other kernel modules …

    metal/inspect/               # ── mod id: pymergetic.metal.inspect ──
      __init__.c                 # capabilities + stub table
      endpoints.c
      app.py                     # Inspect routes (imports microdot)
      stubs.py                   # shared endpoint contract
      adapter_microdot.py        # stubs → MicrodotAdapter
      adapter_fastapi.py         # stubs → FastAPIAdapter (CDN)
      www/inspect/               # SHARED UI
        index.html
        js/
        css/
          base.css
          themes/
            cdn.css
            metal.css
        theme.cdn.json
        theme.metal.json

  crates/pm_metal/               # RS façade only
  typings/pymergetic/metal/
    async/  asgi/  microdot/  inspect/  net/…
  port/common/  boards/
  third_party/
  docs/
```

## How Microdot gets in

| Step | What |
|------|------|
| Source of truth | `src/pymergetic/metal/microdot/*.py` (in-tree CORE, vendored upstream) |
| Mod / VFS | appears as `/mods/pymergetic.metal/microdot/…` |
| Import | Inspect app: `from microdot import …` / package path under that mod |
| C/RS | `include/…/microdot/__init__.h` callers only — do not reimplement Microdot in C |
| Not | wasm pack · not under `pymergetic.metal.inspect` · not top-level `py/` |

## VFS view (runtime)

```text
/mods/pymergetic.metal/
  asgi/…
  microdot/…
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
| stubs · Inspect app · www | `pymergetic.metal.inspect` | contract + UI |
| CDN FastAPI adapter | outside | same contract; theme=cdn |
