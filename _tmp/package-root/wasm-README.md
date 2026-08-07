# Metal WAMR GLUE

Metal owns platform + freestanding libc for wasmmod's `ports/metal/wamr_freestanding.mk`.

| Piece | Path |
|-------|------|
| Platform (`os_malloc` → TLSF, ticks, mutex stubs) | `wasm/port/platform/` |
| Freestanding libc headers/sources | `libc/` |
| BIOS link + `wasm_runtime_init` smoke | `port/boards/X86_64_BIOS` when `ENGINE=mp\|mpwm` |

Engine recipe remains wasmmod OWN. Pack/`import_wasm` faces come later.
