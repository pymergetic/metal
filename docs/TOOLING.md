# Metal unified toolset

Front door for kernel builds, polyglot module codegen, wasm packages, and
source integration. Module layout rules:
[`docs/definitions/module.md`](definitions/module.md).

## Install / invoke

```text
./tools/metal/metal -h
./tools/metal/metal br exp2          # build then run (default target: exp2)
./tools/metal/metal kernel build exp2
./tools/metal/metal kernel run exp2
```

The CLI is a thin Python package under [`tools/metal/`](../tools/metal/).
It **facades** existing `scripts/build.d/…` for kernel work and grows new
commands (`mod`, `pack`, `integrate`) beside them.

**Async shape:** command entrypoints are `async def`; only
`tools/metal/metal` / `metal_cli.cli.main` runs `asyncio.run(...)`.
Small pure helpers (parse, emit strings, path math) may stay sync. This
matches the platform’s async-first model so the same tooling can move
toward MicroPython later without a sync/async rewrite.

## Commands

| Command | Role |
|---------|------|
| `metal mod sync` | Lang pool: `__init__.{ext}` + sibling stems → other faces (`--emit toml` opt-in) |
| `metal mod check` | Validate `__init__.{impl_ext}`; error on banner/stem conflicts |
| `metal mod clean` | Delete generated faces + `.target/` (and legacy `target/`) + managed `.gitignore` block |
| `metal mod ls` | List module trees with generated faces hidden |
| `metal mod build [mem]` | `cargo build --lib` for Rust modules (default `x86_64-unknown-none`) |
| `metal mod test [mem]` | Host `.pm/smoke.{rs\|c\|cpp\|py}` (cargo / cc / python) |
| `metal pack <path>` | Requires `.pm/module` with `type=package` → `build/packages/<name>.wasm` |
| `metal pack inspect <file>` | List manifest + payload files |
| `metal integrate <pkg> --out DIR` | Unpack sources/headers for `-I` / build-against |
| `metal kernel build [efi\|bios\|exp2]` | Wrap product / exp2 builds |
| `metal kernel run [efi\|bios\|exp2]` | QEMU runners |
| `metal br [efi\|bios\|exp2]` | `kernel build` then `kernel run` (default exp2); also `metal kernel br` |
| `metal all` | `mod sync` then `kernel build` (default bios) |

## Package artifact

A Metal **package** is a `.wasm` file: a minimal Wasm module plus a
custom section `metal.pkg` holding:

1. UTF-8 TOML manifest (`name`, `impl`, `version`, …)
2. lz4-compressed ustar of the package file tree (sources, generated
   headers, optional bytecode)

Same blob is used to **run** (future metal-py import) and **build
against** (`metal integrate`).

## Workflow (typical)

```text
# 1. Module hygiene (product and/or exp2 trees with .module)
metal mod check
metal mod sync

# 2. Pack a module subtree (path must exist and be a real module)
metal pack path/to/module

# 3. Build-against
metal integrate build/packages/<name>.wasm --out build/sysroot/<name>

# 4. Kernel — product bios/efi, or exp2 (blank stub until sources exist)
metal kernel build bios
metal kernel run bios
```

`exp2/` is a **blank standalone shell** (directory layout only; no product
overlay). `metal kernel build|run|br exp2` call stub scripts until a
deliberate hello-world slice lives under `exp2/src`. See [`exp2/README.md`](../exp2/README.md).

## Phases

| Phase | Status |
|-------|--------|
| 0 Doc + CLI skeleton | done |
| 1 `kernel` facade | done (efi / bios / exp2 stub) |
| 2 `mod sync\|check\|clean` | done (banner gate; gitignore; clean) |
| 2b **Lang pool** | **fixed** — `c`/`rs`/`py` default emit; `cpp` under `c`; `toml` output-only via `--emit toml` ([`module.md`](definitions/module.md) § "Lang pool") |
| 2c Export matrix | `impl=rs` done; `c`/`cpp`/`py` → catalog later |
| 2d Emit matrix | `.h` + `.pyi` done; consumer `.rs` when `impl!=rs` |
| 2e Export `impl=py` + emit C trampolines into upy | later |
| 2f Keep `metal_cli/mod` MicroPython-portable | ongoing (stdlib-ish only) |
| 3 `pack` / `inspect` | done (wasm + `metal.pkg` lz4/ustar) |
| 4 `integrate` | done (sysroot + clang smoke) |
| 5 Embed list for kernel | done (`embed_list.txt` / `.inc.c` + hook in embed-mods) |
| 6 metal-py import-of-wasm | later |

## Mapping from old scripts

| Old | New |
|-----|-----|
| `scripts/build bios` | `metal kernel build bios` |
| `exp2/scripts/build` | `metal kernel build exp2` |
| `exp2/scripts/run` | `metal kernel run exp2` |
| iface lz4(ustar) packs | `metal pack` / `metal integrate` |
| ad-hoc embed-mods list | `build/packages/embed_list.txt` (phase 5) |
