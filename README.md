# Metal

Muscles for the MetalPython constellation (drivers, NIC, floor, net, …).
**Product build/run is the µPy port — not forge.**

```bash
# from packages/metalpython
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp run
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp REPL=1 run   # see ports/metal/README
make -C ports/metal BOARD=X86_64_BIOS ENGINE=mp live-ssh
make -C ports/metal BOARD=X86_64_UEFI ENGINE=mp run
```

Port forward + smoke markers: [`../../ports/metal/README.md`](../../ports/metal/README.md)
(or [`port/README.md`](port/README.md)).

## Layout

| Path | Role |
|------|------|
| `port/` | BIOS/UEFI µPy board port (`ENGINE=mp\|upy\|mpwm`) |
| `net/`, `async/`, `mem/`, … | C muscles used by the port (tcp/http mini-stack, …) |
| `src/pymergetic/metal/` | Hybrid modules: `__init__.{c,rs,pyi,h}` (one impl, other faces) |
| `include/pymergetic/metal/` | Port muscle headers (`net/tcp.h`, …); module faces live under `src/` |
| `external/` | Submodules (lwip, tlsf, mbedtls, …) |
| `docs/` | Design notes |

**Removed:** `forge-cli`, `src/.../forge/`, forge mod-sync autogen faces,
`.pm/` module crates, `_old/` archive. Do not resurrect.

## Docs

| Doc | Role |
|-----|------|
| [`docs/SOURCETREE.md`](docs/SOURCETREE.md) | Tree + C dialect |
| [`docs/PLATFORM.md`](docs/PLATFORM.md) | BIOS/EFI ops floor |
| [`docs/IO.md`](docs/IO.md) | IO classes |
| [`docs/EFI.md`](docs/EFI.md) | EFI notes |
| [`port/README.md`](port/README.md) | Board smoke / live surfaces |
