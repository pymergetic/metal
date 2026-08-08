# Metal hybrid layout (wasmmod principles)

**Product code builds against `include/` only.** Internals are hybrid C / Rust / Python — one impl, other languages are export faces.

## Layout

```text
include/pymergetic/metal/<mod>/.../*.h   # PUBLIC face (tracked)
src/pymergetic/metal/<mod>/              # hybrid impl + export faces
  __init__.c | __init__.rs | __init__.pyi
port/                                    # -I$(METAL)/include ; link src objects
```

Mirror of wasmmod: `include/` + `glue/`/`crates/pm` + Python faces  
→ Metal: `include/` + `src/...` hybrid + µPy `port/common/mod*.c` / `.pyi`.

## Rules

1. **One ABI per module** — full prefix `pm_metal_<path>_*` (see `.cursor/rules/metal-c-abi-hub.mdc`).
2. **No package-root muscle twins** (`net/`, `async/`, … at repo root). New work lands under `src/` + `include/` only.
3. **Do not grow** short-prefix APIs (`pm_metal_tcp_*`, …). Product net uses full names (`pm_metal_net_ip_*`, `pm_metal_net_ip_tcp_*`, …).
4. **No `external/` vendor pile** — WAMR/µPy come from wasmmod / ENGINE_TOP.

## Exemplar

`src/pymergetic/metal/async/` + `include/pymergetic/metal/async/` — N-runner floor, hybrid faces.

## Net IP (freestanding C)

Port links the freestanding stack at `src/pymergetic/metal/net/ip/` with public
faces under `include/pymergetic/metal/net/ip/` (`pm_metal_net_ip_*`,
`pm_metal_net_ip_tcp_*`, `pm_metal_net_ip_udp_*`). DHCP/DNS are sibling modules
(`net/dhcp`, `net/dns`). The former lwIP RS twin lives under
`_tmp/package-root/net-ip-rs/` — not product. **Do not** resurrect package-root `net/`.

## Transitional leftovers at package root

| Path | Why still there |
|------|-----------------|
| `include/pymergetic/metal/libc` | WAMR freestanding `-nostdinc` headers only — not a second product libc; µPy uses `shared/libc` |
| `third_party/monocypher` + `sha256` | SSH KEX only (tiny); not a second TLS stack |

No `external/`, no `_tmp/`, no package-root muscle twins.
