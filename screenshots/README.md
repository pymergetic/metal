# Screenshots

| File | What |
|------|------|
| `ui-boot.png` | QEMU — shell after boot (METAL banner + device tree) |
| `ui-shell.png` | QEMU — shell with `help` |
| `doom-tab.png` | QEMU — Doom windowed (`tab doom`, ~35 fps tray) |
| `ui-after-doom.png` | QEMU — UI console after Doom (`metal-perf`, tab closed) |
| `uart-after-doom.png` | Host UART — same session as above (UART ⇔ UI console) |
| `uart-doom-create.png` | Host UART — Doom create / `pace 35 Hz` |
| `thinkpad-shell.jpg` | ThinkPad T42p — Metal shell on iron (`radeon_rv370`) |
| `thinkpad-doom.jpg` | ThinkPad T42p — Doom tabbed on iron |
| `pxe-http-sigs.png` | `upload-pxe` — HTTP mirror with `doom.*.aot` / `.wasm` + `.sig` |
| `py-repl.png` | QEMU — boot straight into the Python REPL: boot tree ending `ready` → `python`, rainbow ASCII "MetalPython" banner, Metal + MicroPython version, feature highlight lines (C↔Python, no-GIL tasks, shared async scheduler, isolated contexts, signed wasm natives), `import pmcmd`/`pmcmd.ping(...)` live at the `>>> ` prompt, then `quit()` dropping back to the C shell (`metal:~$`) — one unified scrollable console (no separate input strip), status bar showing the 2-letter keyboard-layout indicator (`us`, left of fps/clock — `Ctrl+Alt+Home` cycles it) |
