# Hello-world slice

API-first. Platform rules: [`PLATFORM.md`](PLATFORM.md).

Product boot path is **inspiration only** (do not copy `BiosPkg/main.c` or
`bios/shim`):

```text
QEMU -kernel
  -> crt0 (Multiboot)
  -> bios main (stdint)
       mem_map -> claim; mem init
       console #0 (ring) -> banner / hello (buffered)
       dev/serial (platform uart half) -> viewport -> drain
       power halt / debug-exit
```

## Build (forge-cli, not archived product default.sh)

| Piece | Where |
|-------|--------|
| Boot | `impl=rs` `{base}.rs` + `catalog.toml`; port ops in `common/` |
| BIOS / EFI | `boot/platform/bios/` `…/efi/` (hidden) C ports only |
| DT | flat Rust `dt.rs` — **no** `common/` |
| mem | `mem.rs` stem + crate-local `arena.rs` / `tlsf.rs` |
| Codegen | `metal mod sync` → `{base}.h` / `{base}.pyi` (banner-owned) |
| Entry asm/ld | under `boot/bios/` when added |
| Build / run | `./forge-cli build|run` |

Link one of `boot/bios` or `boot/efi` per image. Build defines exactly one
`PM_METAL_BOOT_TARGET_*`; ops `.c` files `#error` on mismatch (see PLATFORM.md).

## v1 ops

See PLATFORM.md. Platform: `uart` lower half, mem_map, power. Unified
serial in `dev/serial`; ring in `console/`. Locks use core atomics.

## Shared mem

- Arena + TLSF in `arena.rs` / `tlsf.rs` (crate-local; no codegen).
- Tiny libc (`memset`/`memcpy`) as needed; no product `tlsf_edk2` / shim path.
- Host check: `./tools/metal/metal mod test mem`
  Firmware lib: `./tools/metal/metal mod build mem`

## Explicitly out

- Any EFI/BIOS/Multiboot symbols in `boot/*.h` or shared code (sealed API)
- Fake UEFI under BIOS (`PmBiosUefi.h`, `UINT*`, shim `Library/*`)
- Shared TUs including anything from `boot/<target>/`
- Product overlay / `bios/default.sh` full SRCS
- Wasm, net, shell, gfx, upy (see [`ORCHESTRATION.md`](ORCHESTRATION.md) —
  `reg` / `wasm` / `py` after Metal muscles)
- Growing product `boot/port.h` for early console/mmap
- Forking any module outside `boot/<target>/`

## Success

Serial ASCII: hello then mem PASS, then clean exit or `hlt`.
Image built only from live `src/` + explicitly listed vendored externals
(e.g. TLSF when pulled in).

## Implementation order

1. Sealed `boot` + `dt` + `mem` modules + codegen gate — **done** (stubs)
2. Real BIOS console/mmap + crt0/link + `./forge-cli build bios`
3. Thin hello using boot ops + optional dt smoke
4. EFI ports; Py register later
