# Metal + WAMR Fast JIT — agent build brief

Handoff for enabling **WAMR Fast JIT** on Metal so guests can ship **`.wasm` only** (no per-host `.aot`) on **x86_64**.

Do **not** enable LLVM JIT (`WAMR_BUILD_JIT` / `WASM_ENABLE_JIT`) — that pulls LLVM into the firmware.

## Reality check (read first)

| Fact | Implication |
|------|-------------|
| Upstream Fast JIT codegen is **x86-64 only** (`fast-jit/cg/x86-64`, cmake `FATAL_ERROR` otherwise) | **BIOS i386 / ThinkPad 32-bit is out of scope** unless you write an x86-32 backend. Keep interp + `doom.i386.aot` there. |
| Official claim ≈ **~50% of AOT** perf, not AOT parity | Bench Doom before deleting dual-AOT for x64. |
| Fast JIT needs **asmjit** (C++) + WAMR fast-jit sources | Metal EFI/BIOS builds are mostly C + EDK2 / freestanding clang — **C++ link is the hard part**. |
| Dep: classic interpreter when JIT is on | Keep interp sources; Fast JIT does not replace loader. |
| Code cache default **10 MiB** (`FAST_JIT_DEFAULT_CODE_CACHE_SIZE`) | Size from Metal kheap / `WASM_GLOBAL_HEAP_SIZE` / pool — don’t starve doom’s 16 MiB linear mem. |

**Scope for v1:** EFI **X64** (+ optional BIOS **x86_64**) only. Leave `BUILD_TARGET_X86_32` on interp/AOT.

## Current Metal baseline

- Runtime: `src/pymergetic/metal/guest/wasm/wasm.c` — `wasm_runtime_full_init`, `Alloc_With_Pool`
- Platform: `src/pymergetic/metal/guest/wamr/efi_platform.c` — `os_mmap` = `BH_MALLOC`, `os_mprotect` **stub returns 0** (assumes RAM is executable; AOT already relies on this)
- EFI flags: `src/efi/MetalPkg/Metal.inf` — `WASM_ENABLE_INTERP=1`, `WASM_ENABLE_FAST_INTERP=1`, `WASM_ENABLE_AOT=1`, **no** `WASM_ENABLE_FAST_JIT`
- BIOS: `scripts/build.d/port/efi/…` pattern mirrored in `scripts/build.d/port/bios/default.sh` (WAMR sources listed by hand)
- Doom package: `doom.wasm` + `doom.x86_64.aot` + `doom.i386.aot`; load prefers matching AOT then wasm (`wasm.c` `pm_metal_wasm_run_mod`)
- Offline AOT: `scripts/lib/aot.sh` / `scripts/build.d/port/efi/doom.sh`

## Goal

1. Build Metal EFI X64 with `-DWASM_ENABLE_FAST_JIT=1` (and required sources + asmjit).
2. At init, set running mode so **`.wasm` loads use Fast JIT** (not only interp).
3. Keep AOT load path working (optional); wasm path should not require `.aot`.
4. Smoke: QEMU EFI `run doom` → `metal-doom: create done`, playable; compare rough frame cost vs AOT if easy.
5. Document flag / size delta in this file or `docs/DOOM_ASYNC.md` one-liner.

## Work items

### 1. Vendor asmjit (no FetchContent at Metal build time)

WAMR’s `external/wamr/core/iwasm/fast-jit/iwasm_fast_jit.cmake` fetches asmjit via CMake FetchContent. Metal does **not** use that cmake for EFI — sources are listed in `Metal.inf` / bios `default.sh`.

- Pin asmjit (WAMR tag in that cmake: `c1019f1642a588107148f64ba54584b0ae3ec8d1`) under e.g. `external/asmjit` or `external/wamr/core/deps/asmjit`.
- Build with WAMR’s trimmed defines (from the cmake):  
  `ASMJIT_STATIC`, `ASMJIT_NO_DEPRECATED`, `ASMJIT_NO_BUILDER`, `ASMJIT_NO_COMPILER`, `ASMJIT_NO_JIT`, `ASMJIT_NO_LOGGING`, `ASMJIT_NO_TEXT`, `ASMJIT_NO_VALIDATION`, `ASMJIT_NO_INTROSPECTION`, `ASMJIT_NO_INTRINSICS`, `ASMJIT_NO_AARCH64`, `ASMJIT_NO_AARCH32`.
- Prefer **static** asmjit objects linked into `Metal.efi` / `metal.elf` — no host libc assumptions; strip threading/OS bits if they pull POSIX.

### 2. Add Fast JIT sources to EFI (`Metal.inf`) and BIOS64

From `external/wamr/core/iwasm/fast-jit/`:

- `*.c`, `fe/*.c`
- `cg/x86-64/*.cpp` (C++ — EDK2 must compile/link CXX)

Plus asmjit `src/asmjit/core/*.cpp` and `src/asmjit/x86/*.cpp` (same set as WAMR cmake GLOB).

Includes: fast-jit dir, asmjit `src/`.

Defines (alongside existing WAMR flags):

```text
-DWASM_ENABLE_FAST_JIT=1
```

Do **not** set `WASM_ENABLE_FAST_JIT_DUMP` unless debugging (pulls zydis).

BIOS `i386`: **do not** enable Fast JIT (upstream has no backend).

### 3. C++ in the Metal link

EDK2 `Metal.inf` is C-centric today. You must:

- Compile `.cpp` with a freestanding-capable C++ dialect (`-fno-exceptions -fno-rtti` or project equivalent).
- Satisfy asmjit / fast-jit references (new/delete, or provide freestanding operators that route to Metal heap).
- Keep LTO/`-Werror` from breaking asmjit — may need `-Wno-error` on those files (already used for WAMR).

If EDK2 CXX is too painful, a first spike can be **BIOS x86_64 clang link only**, then port lessons to EFI.

### 4. Runtime init — select Fast JIT mode

In `wasm.c` near `wasm_runtime_full_init`:

- Set `init_args.running_mode` (or post-init `wasm_runtime_set_default_running_mode`) to **`Mode_Fast_JIT`** when `WASM_ENABLE_FAST_JIT` is on and target is x86_64.
- Confirm `wasm_runtime_load` of a `.wasm` actually JITs (log mode once).
- AOT files should still load as AOT module type; don’t break `doom.x86_64.aot` on EFI.

Reference: WAMR `wasm_export.h` running modes (`Mode_Interp`, `Mode_Fast_JIT`, `Mode_LLVM_JIT`, …).

### 5. Memory / executable code

- JIT emits into a code cache via platform mmap/`os_mprotect`. Metal’s `os_mprotect` is already a no-op success — same as AOT.
- Ensure code cache + doom linear memory + ESP caches fit; if OOM, lower `FAST_JIT_DEFAULT_CODE_CACHE_SIZE` via compile define for Metal.
- `os_mmap` ignores `prot` today; keep behavior unless NX appears on a platform.

### 6. Package / product policy (optional follow-up)

After Fast JIT works on EFI x64:

- Runtime can prefer **wasm+JIT** over `doom.x86_64.aot` on x64, or keep AOT-first for max speed.
- HTTP seed (`net_life.c`) can stop requiring x64 `.aot` once JIT is default — **keep i386.aot** for BIOS32.
- Do not delete dual-AOT build until BIOS64 + EFI are both validated.

## Validation

```bash
# from packages/metal
METAL_DOOM_BUILD=1 ./scripts/build efi
./scripts/run efi --bench
# shell: run doom
# expect: load doom.wasm (or aot), metal-doom: create done
```

Also compare:

- EFI + AOT (current)
- EFI + wasm interp (hide `.aot`)
- EFI + wasm Fast JIT (this work)

## Out of scope / do not do

- LLVM JIT / multi-tier JIT in firmware
- Fast JIT on `BUILD_TARGET_X86_32`
- Claiming AMD vs Intel needs different AOTs — `--cpu=generic` x64 AOT already covers that; Fast JIT is about **artifact count × ISA**, not CPU vendor
- Rewriting doomgeneric resolution / renderer

## Key paths

| Path | Role |
|------|------|
| `external/wamr/core/iwasm/fast-jit/` | Fast JIT engine |
| `external/wamr/core/iwasm/fast-jit/iwasm_fast_jit.cmake` | Upstream source/define list + asmjit pin |
| `src/efi/MetalPkg/Metal.inf` | EFI WAMR compile flags + sources |
| `scripts/build.d/port/bios/default.sh` | BIOS WAMR source list / `BUILD_TARGET_*` |
| `src/pymergetic/metal/guest/wasm/wasm.c` | init + mod load |
| `src/pymergetic/metal/guest/wamr/efi_platform.c` | mmap / mprotect |
| `scripts/lib/aot.sh`, `scripts/build.d/port/efi/doom.sh` | offline AOT (keep until JIT proven) |
| `docs/DOOM_ASYNC.md` | Doom guest notes |

## Success criteria

- [ ] `metal.efi` links with Fast JIT + asmjit on X64
- [ ] `run doom` with **only** `doom.wasm` (+ wad) reaches `create done` under Fast JIT
- [ ] BIOS32 unchanged (interp / `doom.i386.aot`)
- [ ] Size/RAM note recorded (efi bytes before/after, code-cache setting)
- [ ] Short bench note vs AOT (even qualitative: “playable / heavier”)
