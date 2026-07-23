# Metal code trust (PKI)

External wasm (ESP / PXE HTTP) and the kernel image are authenticated with
detached ECDSA-P256 signatures. Private keys never live in the Metal source tree.

Runtime verify is **multi-CA**: any baked Mods CA may sign wasm; any baked
Kernel CA may sign the kernel image. Default image ships pymergetic pubs; teams
add their Mods CA cert so their wasm runs beside stock modules.

## Trust mode (builder ↔ loader)

One knob both sides understand: **`METAL_TRUST_MODE`**.

| Mode | Builder | Loader (kernel + ESP wasm) |
|------|---------|----------------------------|
| **off** | stub bake (no CA pubs); skip detach-sign | never verifies |
| **soft** | bake pubs; sign when keys present | **verify if `.sig` present**; missing sig = ok |
| **enforce** | bake pubs required; sign when keys present | **signature required** + must verify |

Defaults:

- PKI present, mode unset → **soft** (dev-friendly)
- no PKI → **off**
- `METAL_TRUST=1` → **enforce** (and bake fails if default realm incomplete)

`build/trust/mode` is written on every bake so scripts can read the effective mode.
Compile-time `PM_METAL_TRUST_MODE` is embedded in `metal_trust_bake.inc.c` so the
loader matches the builder.

Asymmetry (intentional):

- **Unsigned loader** (`off` / `soft` without requiring sigs) can still load
  **signed** modules — soft verifies the `.sig` when present; off ignores it.
- **Enforcing loader** will **not** load unsigned external wasm / unsigned image.
- Embedded (in-ELF) mods are never signature-checked.

Same rule applies mod↔mod: under `enforce`, every ESP wasm needs a valid Mods-CA
sig; under `soft`, unsigned peers are fine; a bad `.sig` always fails.

### Strict boot

| Flag | Effect |
|------|--------|
| default | tree shows `WARN` / `FAIL`, boot continues |
| `METAL_TRUST_STRICT=1` (`-DPM_METAL_TRUST_STRICT=1`) | hard-fail boot on kernel trust FAIL |

Prod recipe:

```bash
METAL_TRUST_MODE=enforce METAL_TRUST_STRICT=1 ./scripts/build efi
```

## Hierarchy (per realm)

```text
Root CA → App CA → Kernel CA | Mods CA(s)
                      │              └── *.wasm.sig
                      └── metal.elf.sig / BOOTX64.EFI.sig
```

No per-module leaf certs — Kernel/Mods CA keys sign artifacts directly.
Multiple realms (and `extra/` drop-ins) bake into one trust store.

## Default composition

| Signer | Typical use |
|--------|-------------|
| **pymergetic** Mods CA | Stock / SDK wasm (`doom`, tests, …) |
| **team** Mods CA(s) | Team-owned wasm on the same ESP/PXE tree |
| **pymergetic** Kernel CA | Metal kernel / EFI image |

Build host bakes all Root + Kernel-CA + Mods-CA publics it finds. Verify tries
each matching CA until one succeeds (OR of signers).

## What is checked when

| When | What |
|------|------|
| **Wasm load** (`run doom`, etc.) | ESP `mods/apps/<name>/<name>.wasm` + optional `.wasm.sig` per mode. Embedded mods: no sig. |
| **Boot** (`-- init` → `trust`) | ESP image + `.sig` (any Kernel CA): tries `EFI/BOOT/BOOTX64.EFI`, `metal.efi`, `metal.elf`. |

Boot tree:

```text
+-- trust        soft|enforce|off   # floor: policy mode
`-- init
|   +-- trust    ok|WARN CA parse|FAIL kernel sig|off
|   `-- shell    ok
|
`-- ready        ok

READY

```

Floor uses `pm_metal_trust_mode_str()`. Parse / verify run under `-- init` after
mbedTLS heap hooks are installed (`pm_metal_mbedtls_runtime_init`).

Trust / ok / WARN / FAIL / READY stay on the tree line (styled); no FIGlet
interrupt for WARN/FAIL.

## Host setup

```bash
export METAL_PKI_DIR=~/.local/share/metal/pki   # default if unset
./scripts/pki init                 # once: default pymergetic CAs + bake
./scripts/pki init-realm acme      # team realm (Root→App→Mods); optional --with-kernel
./scripts/pki bake                 # refresh build/trust/metal_trust_bake.inc.c + mode
./scripts/pki sign-wasm build/doom/doom.wasm
./scripts/pki sign-wasm path/team.wasm --realm=acme
./scripts/pki sign-elf  build/bios/i386/metal.elf
./scripts/pki sign-elf  build/efi/metal.efi

# Dev: no trust
METAL_TRUST_MODE=off ./scripts/build efi

# Dev with optional sigs (default when PKI exists)
METAL_TRUST_MODE=soft ./scripts/build efi

# Prod
METAL_TRUST_MODE=enforce METAL_TRUST_STRICT=1 ./scripts/build efi
```

| Env | Meaning |
|-----|---------|
| `METAL_PKI_DIR` | PKI home (keys + certs) |
| `METAL_TRUST_MODE` | `off` / `soft` / `enforce` |
| `METAL_TRUST=1` | ⇒ enforce + bake fails if default realm incomplete |
| `METAL_TRUST_STRICT=1` | boot hard-fail on kernel trust FAIL |

Build (bios/efi) always runs `pki bake`. Stage copies `*.sig` next to the image
when mode ≠ off and keys exist (PXE / ESP).

## Layout under `METAL_PKI_DIR`

Flat default (what `init` creates):

```text
root/{ca.key,ca.crt}
app/{ca.key,ca.crt}
kernel/{ca.key,ca.crt}
mods/{ca.key,ca.crt}
```

Additional teams:

```text
realms/<id>/
  root/{ca.key,ca.crt}
  app/{ca.key,ca.crt}
  mods/{ca.key,ca.crt}
  kernel/…          # optional (--with-kernel)
extra/mods/<id>.crt # Mods CA pub only (team drop-in, no private key)
extra/roots/<id>.crt
```

`bake` collects: default realm + every `realms/*` + `extra/mods/*` + `extra/roots/*`.
`init-realm` also copies the new Mods CA into `extra/mods/<id>.crt` for easy redistribution.
