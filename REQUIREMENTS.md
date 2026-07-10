# pymergetic-metal — requirements

OS core on Zephyr: boot, **machine RAM + link footprint + k_pool + malloc tail**, portable **`.o` module loading**, and **`pm_metal_port`** shims. No CPython here — see `packages/kernel/REQUIREMENTS_PYTHON.md`.

Same behavior on **fake metal** (`native_sim`) and **real metal** (QEMU / HW).

---

## Phase 0 — Scope

- [x] **Targets**: `native_sim/native/64`, `qemu_x86_64`, eventual HW board
- [x] **Machine RAM**: `boards/pm_machine_ram.dtsi` → `CONFIG_SRAM_SIZE`
- [x] **Kernel used**: link footprint to `_end` (reported, not configured)
- [x] **k_pool**: `CONFIG_HEAP_MEM_POOL_SIZE` (`k_malloc`)
- [x] **malloc heap**: SRAM tail via `COMMON_LIBC_MALLOC_ARENA_SIZE=-1`
- [ ] **Mod model**: portable `.o` blobs linked against kernel API; load into app-static slots
- [x] **Port layer**: malloc heap region ops (v0); stdio/time later

**Verification:** one `prj.conf`; board overlays only for machine RAM; see `docs/MEMORY_BASELINE.md`.

---

## Phase 1 — Zephyr shell

**`external/zephyr/`** · `v4.4.0` (west manifest)

- [x] west workspace in `packages/metal/` (`.west/config`, `west-manifest/west.yml`)
- [x] hello app in `runtime/zephyr/` (`west build -b native_sim/native/64 runtime/zephyr`)
- [x] `include/pymergetic/metal/` + `src/pymergetic/metal/` layout (boot, ram, port)
**Verification:** `scripts/verify-twin-targets.sh` builds both targets; boot prints layout + malloc smoke test.

---

## Phase 2 — Heap (`port/`)

- [x] Boot probe: machine, kernel used, k_pool, malloc arena
- [x] malloc heap region → picolibc/Zephyr `malloc` (`COMMON_LIBC_MALLOC_ARENA_SIZE=-1`)
- [x] `k_malloc` → `CONFIG_HEAP_MEM_POOL_SIZE`

**Verification:** allocation smoke test in `main`; no custom arena layer.

---

## Phase 3 — Mod loader (`mod/`)

- [ ] Parse/load relocatable `.o` into reserved app-static region
- [ ] Resolve symbols against kernel export table
- [ ] Slot accounting vs app heap

**Verification:** load a trivial test mod; call one exported entry point.

---

## Phase 4 — Port layer (`port/`)

- [x] malloc heap region on Zephyr + native_sim
- [ ] `pm_metal_port` stdio / time headers
- [ ] Hooks for kernel package without CPython in metal

**Verification:** kernel smoke test can link against `pm_metal_port` without pulling CPython into metal.

---

## Appendix — Zephyr built-ins vs kernel needs

Zephyr provides picolibc, optional mbedTLS, UUID via `CONFIG_UUID` — **not** desktop `libz`, OpenSSL, libffi, etc. Those are **kernel** vendored deps, not metal submodules.
