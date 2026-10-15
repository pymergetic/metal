# Verification

What is proven about the `jit/rs` self-hosting Rust→C compiler (`pymergetic.metal.jit.rs`),
and how to re-run each proof. The compiler is a **subset**: it accepts a documented
Rust slice and **refuses** (with `rsx: unsupported: ...`) anything outside it rather
than mis-lowering it. The boundary is kept honest by the four layers below.

To re-run **everything** unattended, `tools/overnight.sh` chains prove-all + the
self-host cycle + the upywm unix seat + the wasmmod engine tests into a timestamped
log dir with a `SUMMARY.txt` (it defaults to `EMSDK=/home/ladmin/emsdk`).

## 1. Host test suite

```
make -C extmod/metal build/metal-async-test   # 161 run, 0 not clean
```

Includes `test_self_host` (two independent compiles must produce **byte-identical**
C), `test_self_host_object`, and `test_self_host_link` (the emitted object and a
linked image both build). Cargo builds with **0 warnings**
(`--no-default-features --features upy-host`).

## 2. The self-host cycle (the bootstrap)

```
make -C extmod/metal selfhost                 # SELFHOST_EXIT=0 when ALL STAGES PASSED
```

The compiler compiles **itself**, and the self-hosted compiler compiles itself again
until the output stops changing. Each stage guards one way the chain could lie:

| Stage | What it proves |
|---|---|
| 0. rebuild boot | cargo + the `selfhost_feed` corpus are in sync |
| 1. gen-1 | the **boot** compiler compiles the compiler |
| 2. build self | the gen-1 C builds into the in-kernel "self" compiler |
| 3. gen-2 | the **self** compiler compiles the compiler |
| 4. fixed point | `gen-2 == gen-1` (byte-identical, **2,302,959 bytes**) |
| 4b. in-kernel object | self compiles `__impl__.rs`; TCC makes the object |
| 4c. in-kernel link | the object **links**; the *linked* compiler reproduces gen-1 |
| 5. corpus | boot and self agree on every fixture; goldens match when present |
| 6. c++ chain | `lower → TCC object → in-kernel link → run` for the C++ card |
| 6b. c++ selfhost | the card transpiles **its own source**, in-kernel (**244,403 bytes** identical) |
| 7. ksweep | **every card** (96/96) survives the in-kernel compile chain |

"Byte-identical" is the point: a non-deterministic or order-dependent emit anywhere
in the compiler breaks the fixed point.

## 3. Self-host corpus

```
tools/selfhost_corpus/*.rs -> *.c   # 14/14 byte-identical via ./build/selfhost_feed
```

Each fixture is a Rust slice that must lower to its checked-in `.c` golden
**byte-for-byte**, on both the boot and the self compiler. The corpus pins the
accepted subset feature-by-feature (sum types, `match`, iterators, `Result`/`?`,
traits + dispatch, generics, `let`-destructuring, `macro_rules!`, closure-as-value, …).

## 4. Seats (every platform)

Per `REPO_LAYOUT.md`, a feature is not done until every seat that can build here is
proven. Each row is `make -C extmod/metal <target>` / `make -C port BOARD=<board> prove`:

| Seat | Command | Status |
|---|---|---|
| host C | `make build/metal-async-test` | ✅ 161/0 |
| self-host (full bootstrap) | `make selfhost-cycle` | ✅ ALL STAGES PASSED |
| firmware X86 BIOS | `make -C port BOARD=X86_64_BIOS prove` | ✅ |
| firmware X86 UEFI | `make -C port BOARD=X86_64_UEFI prove` | ✅ |
| firmware ARMV7 QEMU | `make -C port BOARD=ARMV7_QEMU prove` | ✅ boots in `qemu-system-arm` |
| firmware ARMV7 RV1106 | `make -C port BOARD=ARMV7_RV1106 prove` | ✅ `metal.bin` image |
| unix µPy | `make -C extmod/metal upy` | ✅ "guest prove ok" |
| browser (emcc/wasm32) | `make -C extmod/metal browser` | ✅ exit 0 (49 panel asserts) |

The browser seat needs `EMSDK` pointed at an activated emsdk
(`export EMSDK=/home/ladmin/emsdk; export PATH="$EMSDK/upstream/emscripten:$PATH"`).
The ARMV7 seats build with **clang** (`--target=armv7-none-eabihf -nostdlib
-fuse-ld=lld`) + `rustc --target armv7a-none-eabihf`, not `arm-none-eabi-gcc`.

## The subset boundary

Accepted (compiles + runs on every seat above): let/const/static, if/match with
payloads, guards and struct variants, iterator `for`, sum types and `Result<T,E>`
with `?`, struct/tuple/tuple-struct `let`-destructuring with `..`/`mut`, nested
`fn`, traits + dynamic dispatch, generics (monomorphized), single-arity
`macro_rules!`, and capture-less closures as values.

Refused, with a precise diagnostic (deliberate — a refusal beats a wrong lowering):
capturing closures **as a general value** (the synthesized fn is a plain C function
with no env slot; captures only work where the closure body is inlined — the
`.map`/`.all`/`.any`/`.position`/`.unwrap_or_else` combinators), `dyn Trait` trait
objects as first-class values, async blocks, and `macro_rules!` beyond one fixed
arity. Each refusal names the construct so the boundary is discoverable.
