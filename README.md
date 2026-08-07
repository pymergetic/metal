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
| `src/pymergetic/metal/` | Hybrid impl + RS/Py export faces (`__init__.{c,rs,pyi}`) |
| `port/` | BIOS/UEFI µPy boards (`ENGINE=mp\|upy\|mpwm`); `-I include` |
| `external/` | Vendors (lwip, tlsf, mbedtls, …) |
| `_tmp/` | Quarantine for old parallel trees — **not** product |
| `docs/HYBRID.md` | Layout rules |

**Do not** grow package-root `net/` / `async/` / `mem/` / … twins. Migrate into
`src/` + `include/` or quarantine under `_tmp/`.

**Removed:** `forge-cli`, forge mod-sync, `.pm/` crates, `_old/`. Do not resurrect.

## Docs

| Doc | Role |
|-----|------|
| [`docs/HYBRID.md`](docs/HYBRID.md) | include + hybrid C/RS/Py |
| [`docs/IO.md`](docs/IO.md) | IO classes + N runners |
| [`docs/SOURCETREE.md`](docs/SOURCETREE.md) | Tree + C dialect (may lag; HYBRID wins) |
| [`port/README.md`](port/README.md) | Board smoke / live surfaces |
