# metal CLI

```bash
./tools/metal/metal mod check
./tools/metal/metal mod sync
./tools/metal/metal mod build boot
./tools/metal/metal mod test mem          # host .pm/smoke.*
```

Module metadata: `.pm/module` (JSON, `type` = module|package|hidden).
Rust crates: `.pm/Cargo.toml`. See `docs/definitions/module.md` and `docs/TOOLING.md`.

Rust forge (experimental, not authoritative yet). App: `tools/forge-cli/`;
module: `exp2/.../forge/` (port shims only for solo/metal).

```bash
./tools/forge mod check
./tools/forge mod sync
```
