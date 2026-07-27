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
