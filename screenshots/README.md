# Screenshots

| File | What |
|------|------|
| `py-introspect.png` | QEMU — hero: MetalPython banner kept in frame + short REPL introspection (`mem.limit.get`, `externals` versions). Top image in the main README. Regenerate: `python3 scripts/capture-readme-shot.py` (keep demo short or the banner scrolls off) |
| `ui-boot.png` | QEMU — shell after boot (METAL banner + device tree) |
| `ui-shell.png` | QEMU — shell with `help` |
| `py-repl.png` | QEMU — boot into REPL: rainbow banner, feature lines, `import pmcmd` / `pmcmd.ping(...)`, `quit()` back to C shell; status bar keyboard-layout indicator (`us`/`de`, `Ctrl+Alt+Home`) |
| `doom-tab.png` | QEMU — Doom windowed (`tab doom`, ~35 fps tray) |
| `ui-after-doom.png` | QEMU — UI console after Doom (`metal-perf`, tab closed) |
| `uart-after-doom.png` | Host UART — same session as above (UART <=> UI console) |
| `uart-doom-create.png` | Host UART — Doom create / `pace 35 Hz` |
| `thinkpad-shell.jpg` | ThinkPad T42p — Metal shell on iron (`radeon_rv370`) |
| `thinkpad-doom.jpg` | ThinkPad T42p — Doom tabbed on iron |
| `pxe-http-sigs.png` | `upload-pxe` — HTTP mirror with `doom.*.aot` / `.wasm` + `.sig` |
| `menuconfig.png` | Host — `./scripts/menuconfig` (Build + `pymergetic.metal` menus) |
| `iface-packages.png` | QEMU HTTP — `/iface` pack catalog (`metal.guest` + ESP sidecars + `mod.t8_multimod_lib`) |
| `iface-headers.png` | QEMU HTTP — `/iface/pkg/metal.guest` (headers pack file list) |
| `iface-sources.png` | QEMU HTTP — `/iface/pkg/metal.guest.sources` (sources pack; Kconfig on) |
| `iface-source-view.png` | QEMU HTTP — highlighted source view (`lz4.c` in `metal.guest.sources`) |
| `iface-syms.png` | QEMU HTTP — `/iface/sym` (scraped NativeSymbol table) |
| `asgi-dispatch.png` | Source — ASGI `asgi_server.c` mount → runner kind (`C` / `PY` / `WASM`) |
| `asgi-httpd-mounts.png` | Source — `mods/etc/httpd.json` mounts (`c:health` / `c:static` / `py:microdot`) |
| `http-home.png` | QEMU HTTP — `/` catalog home (docs / iface / syms / externals / limits counts) |
| `externals.png` | QEMU HTTP — `/externals` (Dropbear / lwIP / mbedTLS / µPy / microtar / WAMR) |
| `limits.png` | QEMU HTTP — `/limits` (compile-time mem/buffer budgets from `mem.limit`) |
| `docs-shell.png` | QEMU HTTP — `/docs?kind=shell` (console / pmcmd catalog + filter legend) |
| `docs-mod-empty.png` | QEMU HTTP — `/docs?kind=mod` before any guest load (empty table) |
| `docs-mod-load.png` | QEMU — REPL `pmcmd.load("doom")` → `load: ready` (ESP staged) |
| `docs-mod-doom.png` | QEMU HTTP — `/docs?kind=mod` after load (`doom.run` row) |
