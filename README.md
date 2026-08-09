# Metal

Muscles for the MetalPython constellation (drivers, NIC, floor, net, …).
**Product build/run is the µPy port — not forge.**

```bash
# from packages/metalpython
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp run
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp repl        # interactive >>>
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp repl-check  # scripted prove
make -C ports/metal BOARD=X86_64_UEFI ENGINE=mp run
```

Port: [`port/README.md`](port/README.md).

## Layout

| Path | Role |
|------|------|
| [`include/pymergetic/metal/`](include/README.md) | **Public** C faces |
| `src/pymergetic/metal/` | Hybrid impl |
| `port/` | BIOS/UEFI µPy boards |
| `include/.../libc` | WAMR freestanding headers only (`-nostdinc`); µPy uses `shared/libc` |
| `third_party/tlsf` | Tiny allocator used by floor |
| `third_party/monocypher` | X25519 + Ed25519 (SSH KEX); optional Ed25519/SHA-512 |
| `third_party/sha256` | SHA-256 for `curve25519-sha256` |
| [`docs/HYBRID.md`](docs/HYBRID.md) | Rules |

**Not in metal:** `external/` vendor pile, `_tmp/`, package-root muscle twins.
WAMR lives in **wasmmod**. µPy is **ENGINE_TOP**. No second copies here.

## Docs

| Doc | Role |
|-----|------|
| [`docs/HYBRID.md`](docs/HYBRID.md) | include + hybrid |
| [`docs/SOURCETREE.md`](docs/SOURCETREE.md) | path == module tree |
| [`docs/MODULE_MATRIX.md`](docs/MODULE_MATRIX.md) | export faces × async compliance (human export; seats SoT is `reg/seats.c`) |
| [`tests/matrix/`](tests/matrix/) | ledger + browser import smoke (`make -C tests/matrix all`) |
| [`include/SYMBOLS.md`](include/SYMBOLS.md) | C ↔ RS ↔ Py symbol ledger |
| [`port/README.md`](port/README.md) | smoke / REPL / live |
