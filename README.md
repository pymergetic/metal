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
| `port/` | BIOS/UEFI µPy boards; `-I include` (+ transitional `-I libc` symlink) |
| `libc` → `include/.../libc` | Symlink for freestanding `-I` |
| `external/` | Vendors |
| `_tmp/` | Quarantine — **not** product |
| [`docs/HYBRID.md`](docs/HYBRID.md) | Rules |

**Gone from package root:** `net/` `async/` `mem/` `bus/` `console/` `draw/` `shell/` `dev/` `wasm/` product trees.

**Net:** freestanding C under `src/.../net/ip/` + `include/.../net/ip/` (full
`pm_metal_net_ip_*` prefix). Former lwIP RS twin quarantined in `_tmp`.

## Docs

| Doc | Role |
|-----|------|
| [`docs/HYBRID.md`](docs/HYBRID.md) | include + hybrid C/RS/Py |
| [`docs/IO.md`](docs/IO.md) | IO classes + N runners (aspirational / host-era notes) |
| [`port/README.md`](port/README.md) | Board smoke / live surfaces |
