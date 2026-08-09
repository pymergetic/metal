# Arch · port · wamr_host · wasmmod

## Law (non-negotiable)

**Same boot behaviour on every seat.** ThinkPad (BIOS/UEFI) and browser
`arch.wasm` run the **same** bring-up sequence, the **same** boot tree, the
**same** `ready ok`, the **same** rainbow MetalPython, then the **same**
post-ready path into the shell.

The **only** allowed differences:

| Diff | Why |
|------|-----|
| **Artifact format** | Browser = Emscripten `.mjs`/`.wasm`; box = Multiboot / PE EFI |
| **HAL / drivers** | Browser = fake/sim backends (mem, console→panel, nic→`fetch`, …); box = real ACPI/virtio/UART |
| **Console face** | Prefer UART-style text shell everywhere for parity; FB/GOP may be absent or stubbed in browser if µPy/web can’t do gfx yet — **not** a different boot script |

**Forbidden:**

- A second boot written in Python (`tree_py`, CDN “autoexec” that prints the tree)
- Forge `boot/platform/*/main` beside `port/boards/*/main`
- “Browser-shaped” ready banners that don’t match the box

**autoexec** (DOS meaning): runs **after** `ready` **and** `mp_init` — bind
metal-cdn (all seats) + import hook. Not boot.

**CDN:** platform default (`METAL_CDN_URL`) on every seat. DHCP/iPXE option 224
may `add` (prepend, default) or `replace:URL` / `off`. Browser session autoexec
may replace with the live origin.

## Four spines

| Spine | Job | Path |
|-------|-----|------|
| **arch** | Seat id + which sim/real HAL leaf labels | `src/.../metal/arch/{x86,x86_64,wasm}` |
| **port** | **Only** µPy + firmware/browser entry | `port/boards/*`, `port/hal/*`, `port/webassembly`, `port/boot/*` |
| **wamr_host** | Box hosts guest `.wasm` packs (WAMR) | `src/.../metal/wamr_host/` |
| **wasmmod** | Shared pack engine | `extmod/wasmmod/` |

## Build-time arch id

```text
-DPM_METAL_CFG_ARCH_X86_64=1 -DPM_METAL_CFG_FW_BIOS=1     # boards/X86_64_BIOS
-DPM_METAL_CFG_ARCH_X86_64=1 -DPM_METAL_CFG_FW_UEFI=1     # boards/X86_64_UEFI
-DPM_METAL_CFG_ARCH_WASM=1   -DPM_METAL_CFG_FW_BROWSER=1  # port/webassembly
```

## Boot (one C path)

```text
board / emscripten main
  → pm_metal_boot()          # live tree → ready → rainbow
  → mp_init()
  → pm_metal_autoexec()      # bind CDN list (home ± site)
  → install_hook()           # when MICROPY_PY_WASM
  → REPL / UART shell
```

Tree: **`boot/tree.c`** (live enter/item/leave) linked into native **and** browser `mp`.
No Py mirror.

## CDN engines

| Pill | Build / shelf |
|------|----------------|
| **mp** | Lead pack `pymergetic.metal.arch.wasm` (`.mjs` + `.wasm`); same metal boot + sim HAL |
| **mpwm** | wasmmod only — **not** metal boot parity (`pymergetic.wasmmod`) |
| **upy** | vanilla — **not** metal |

Arch seats on the CDN shelf: `pymergetic.metal.arch.{x86,x86_64,wasm}`. The UI `mp` pill loads **`arch.wasm` lead artifacts** (not a separate private download site).
