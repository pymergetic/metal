# Metal tooling

Front door for freestanding firmware builds and polyglot module codegen.
Module layout: [`definitions/module.md`](definitions/module.md).

## Invoke

```text
git submodule update --init --recursive   # lwip tlsf monocypher mbedtls
./forge-cli build bios                    # or efi | all
./forge-cli run bios
./forge-cli mod check|sync|ls|clean
./forge-cli config edit|gen|old
./forge-cli img rootfs|mtar|…             # image helpers
./forge-cli stress                        # optional QEMU stress
./forge-cli --fresh version               # rebuild forge binary then run
```

**Forge (Rust):** [`src/pymergetic/metal/forge/`](../src/pymergetic/metal/forge/)
is a normal `.pm` module (codegen + image/build/run). The Linux app is the
hidden crate [`forge/cli/`](../src/pymergetic/metal/forge/cli/), launched by
package-root [`./forge-cli`](../forge-cli) (autobuild into `forge/cli/.target`).

Vendors are **git submodules** under `external/` — forge does not clone or
download them. Missing trees fail with `git submodule update --init …`.

Kconfiglib wrappers: [`forge/_kconfig/`](../src/pymergetic/metal/forge/_kconfig/).
Retired: live `scripts/`, Python `tools/metal`, vendor `patches/`, `exp2/`
wrapper directory — all under [`_old/`](../_old/) or removed.

## Layout (live)

```text
packages/metal/
├── forge-cli                 # bash launcher
├── src/pymergetic/metal/     # firmware + forge module
├── config/                   # Kconfig (+ local .config)
├── build/                    # forge outputs (gitignored)
├── external/                 # submodules (lwip, tlsf, …)
├── stress/                   # optional stress TUs
├── docs/                     # live design + tooling docs
└── _old/                     # archived product tree / scripts / patches
```

## Forge commands

| Command | Role |
|---------|------|
| `forge mod sync\|check\|clean\|ls` | Lang-pool faces (banner gate); `check` also verifies c/rs/py fn symmetry |
| `forge convert SRC DST` | One-shot face convert |
| `forge config edit\|gen\|old` | exp2-style Kconfig under `config/` |
| `forge build [bios\|efi\|all]` | config gen → rootfs img → mod sync → link |
| `forge run [bios\|efi\|all]` | QEMU |
| `forge img …` | mtar / fat / zip / rootfs helpers |
| `forge stress` | QEMU stress harness |
| `forge version` | Version string |

## Vendors

| Submodule | Role |
|-----------|------|
| `external/lwip` | IP stack (`forge build` links today) |
| `external/tlsf` | Heap (`mem/tlsf` today) |
| `external/monocypher` | Crypto (AEAD/hash) — product `util/crypto`; live wire-up next |
| `external/mbedtls` | TLS on lwIP — product net/tls; live wire-up next |
| `external/edk2` | EFI headers (`edk2-stable202502`) — `forge build efi` |

Stack intent: **lwIP + mbedTLS + Monocypher** (see archived `_old/scripts/setup.d/deps/net.sh`).
Submodules pin the trees; forge does **not** clone them.

EFI: `external/edk2` submodule (pin `edk2-stable202502`) for `MdePkg/Include`:

```bash
git submodule update --init external/edk2
```

`util/lz4` / `util/tar` stay **in-tree Rust**.

## Workflow

```text
git submodule update --init --recursive
./forge-cli mod check
./forge-cli build bios
./forge-cli run bios
```

## Phases (done)

| Phase | Status |
|-------|--------|
| Lang pool `mod sync/check/clean` in forge | done |
| `forge build\|run` bios (and efi path) | done |
| Nested `forge/cli` + `./forge-cli` launcher | done |
| Dissolve `exp2/` → package root | done |
| Retire live `scripts/` + Python `tools/metal` | done (`_old/`) |
| Vendors = git submodules (no forge download) | done |
| Retire live `patches/` | done (`_old/patches/`) |

## Roadmap (next)

Ordered for the live freestanding tree — do not grow `_old/` product surface.

1. **EFI** — green `./forge-cli run efi`; keep [`EFI.md`](EFI.md) honest.
2. **Docs / IDE** — keep `docs/` + `.clangd.template` aligned with the flat
   tree; one-shot clangd refresh if editor noise returns (no `setup ide`).
3. **Forge completeness** — host smokes (`mod test` equivalent), optional
   pack/integrate if guest packages return; no Python metal revival.
4. **Async / IO floor** — deepen drivers against [`PLATFORM.md`](PLATFORM.md)
   + [`IO.md`](IO.md) / [`definitions/async/`](definitions/async/); keep
   stackless + host-heap await rules.
5. **Guest mods (later)** — when `guest/mod` returns, revive [`MODS.md`](MODS.md)
   against live async; µPy / WASI stay `_old/` until deliberately re-homed.

## Mapping from archived tools

| Old | New |
|-----|-----|
| `scripts/build bios` | `./forge-cli build bios` |
| `scripts/setup <dep>` | `git submodule update --init external/<dep>` |
| `tools/metal metal mod …` | `./forge-cli mod …` |
| `tools/forge` symlink | removed — use `./forge-cli` |
| `exp2/…` paths | package-root `src/`, `config/`, `build/` |
