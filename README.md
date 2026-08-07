# Metal

Muscles for the MetalPython constellation (drivers, NIC, floor, net, …).
**Product build/run is the µPy port — not forge.**

```bash
# from packages/metalpython
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp run
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp live-ssh
make -C ports/metal BOARD=X86_64_UEFI ENGINE=mp run
```

Port forward + smoke: [`../../ports/metal/README.md`](../../ports/metal/README.md)
(or [`port/README.md`](port/README.md)).

## Layout (wasmmod principles)

| Path | Role |
|------|------|
| [`include/pymergetic/metal/`](include/README.md) | **Public** C faces — enough to build against |
| `src/pymergetic/metal/` | Hybrid impl + RS/Py export faces |
| `port/` | BIOS/UEFI µPy boards; `-I include` (+ transitional `-I libc`) |
| `libc/*.h` | Transitional freestanding headers for `-I` (`.c` under `src/.../libc/port/`) |
| `wasm/port/platform/*.h` | Transitional WAMR platform headers (`.c` under `src/`) |
| `external/` | Vendors |
| `_tmp/` | Quarantine — **not** product |
| [`docs/HYBRID.md`](docs/HYBRID.md) | Rules |

**Gone from package root:** `net/` `async/` `mem/` `bus/` `console/` `draw/` `shell/` `dev/` product `.c` trees.

**Still open:** short-prefix mini-IP (`net/minip` + flat `include/.../net/tcp.h`) vs RS `net/ip`; full prefix rename; libc/wasm header relocate into `include/`.

## Docs

| Doc | Role |
|-----|------|
| [`docs/HYBRID.md`](docs/HYBRID.md) | include + hybrid C/RS/Py |
| [`docs/IO.md`](docs/IO.md) | IO classes + N runners |
| [`port/README.md`](port/README.md) | Board smoke / live surfaces |
