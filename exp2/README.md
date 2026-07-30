# exp2 — blank standalone package shell

Directory layout mirrors the product metal package so later work keeps the
same mental map ([`docs/SOURCETREE.md`](../docs/SOURCETREE.md)). There is
**no runnable firmware yet**. Product is **inspiration only** — no overlay,
no fallthrough compile of `../src`, no fake BIOS-UEFI shim.

```text
exp2/
  src/pymergetic/metal/boot/   # floor + ports (Rust + bios/efi C)
  src/pymergetic/metal/dt/     # device table (Rust)
  src/pymergetic/metal/mem/    # host heap (Rust bump stub) — no runtime/ prefix
  scripts/ docs/
```

Flat module roots: human `{base}.{ext}` + `catalog.toml`; public faces
from `metal mod sync` (in-file banner write gate). `common/` only on
`boot/` (bios/efi C ports).

## Design (read first)

| Doc | Role |
|-----|------|
| [`docs/PLATFORM.md`](docs/PLATFORM.md) | BIOS/EFI ops-struct floor, dialect, C then Rust then Py |
| [`docs/HELLO_SLICE.md`](docs/HELLO_SLICE.md) | First hello dependency / order |

Summary: **`boot`**: Rust `{base}.rs` + `common/` port ops + bios/efi C.
**`dt` / `mem`**: Rust `{base}.rs` only (no `common/`). WASI → `.package`.

## Scripts

| Script | Role now |
|--------|----------|
| `scripts/sync-tree` | Recreate empty dirs from product `src/` + package skeleton |
| `scripts/build` | Stub — exits until a real minimal build exists |
| `scripts/run` | Stub — exits until a firmware image exists under `build/` |

CLI: `./tools/metal/metal kernel build|run|br exp2` facades those scripts.
`./tools/metal/metal mod sync` generates module projections.

```
./exp2/scripts/sync-tree
./tools/metal/metal mod check
./tools/metal/metal mod sync
```
