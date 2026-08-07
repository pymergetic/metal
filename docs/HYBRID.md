# Metal hybrid layout (wasmmod principles)

**Product code builds against `include/` only.** Internals are hybrid C / Rust / Python — one impl, other languages are export faces.

## Layout

```text
include/pymergetic/metal/<mod>/.../*.h   # PUBLIC face (tracked)
src/pymergetic/metal/<mod>/              # hybrid impl + export faces
  __init__.c | __init__.rs | __init__.pyi
port/                                    # -I$(METAL)/include ; link src objects
_tmp/                                    # quarantine for old parallel trees (not product)
```

Mirror of wasmmod: `include/` + `glue/`/`crates/pm` + Python faces  
→ Metal: `include/` + `src/...` hybrid + µPy `port/common/mod*.c` / `.pyi`.

## Rules

1. **One ABI per module** — full prefix `pm_metal_<path>_*` (see `.cursor/rules/metal-c-abi-hub.mdc`).
2. **No package-root muscle twins** (`net/`, `async/`, … at repo root). New work lands under `src/` + `include/` only.
3. **Do not grow** short-prefix APIs (`pm_metal_tcp_*`, …). Migrate or quarantine.
4. **`_tmp/`** is quarantine — not a second product path.

## Exemplar

`src/pymergetic/metal/async/` + `include/pymergetic/metal/async/` — N-runner floor, hybrid faces.

## Transitional: C mini-IP (`net/minip`)

Port still links the freestanding mini-IP stack at
`src/pymergetic/metal/net/minip/` (`pm_metal_tcp_*` short names in
`include/.../net/tcp.h`). RS `src/.../net/ip/` (lwIP) is a twin — demote or
merge later. **Do not** resurrect package-root `net/`.

## Transitional leftovers at package root

| Path | Why still there |
|------|-----------------|
| `libc/*.h` | Freestanding `-I$(METAL)/libc` for port TUs; `.c` lives under `src/.../libc/port/` |
| `wasm/port/platform/*.h` | Platform include path for WAMR; `.c` under `src/.../wasm/port/platform/` |
| `_tmp/package-root/` | Quarantined old async/ and leftovers |

No package-root `net/` `async/` `mem/` `bus/` `console/` `draw/` `shell/` `dev/` product `.c` trees.
