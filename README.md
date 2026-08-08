# Metal

Muscles for the MetalPython constellation (drivers, NIC, floor, net, …).
**Product build/run is the µPy port — not forge.**

```bash
# from packages/metalpython
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp run
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp repl
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
| [`docs/HYBRID.md`](docs/HYBRID.md) | Rules |

**Not in metal:** `external/` vendor pile, `_tmp/`, package-root muscle twins.
WAMR lives in **wasmmod**. µPy is **ENGINE_TOP**. No second copies here.

## Docs

| Doc | Role |
|-----|------|
| [`docs/HYBRID.md`](docs/HYBRID.md) | include + hybrid |
| [`port/README.md`](port/README.md) | smoke / REPL / live |
