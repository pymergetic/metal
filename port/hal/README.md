# port/hal — seat drivers

Same boot (`port/boot/boot.c`) on every seat. Only HAL differs:

| Leaf | Seat | Role |
|------|------|------|
| `bios/` | Multiboot firmware | real UART / board hooks |
| `efi/` | UEFI firmware | real UART / board hooks |
| `wasm/` | browser `arch.wasm` | fake/sim backends |

Board `main.c` and Emscripten entry stay under `port/boards` / `port/webassembly`.
Legacy forge code under `src/.../boot/platform` is not an entry path.
