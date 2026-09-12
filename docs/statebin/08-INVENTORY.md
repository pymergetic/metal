# Substrate inventory

What the tree has today against what the artifact needs, with evidence. Paths are
relative to `packages/metalpython/extmod/` unless noted. Every "no" here was checked by
search, not assumed.

## Per claim element

| Claim element | Today | Where | Verdict |
|---|---|---|---|
| Bootable section table + header | none; but `.bootinfo` is a magic-branded loader-writable header at a fixed offset | `metal/port/boards/X86_64_BIOS/crt0.S:5`, patched at `trampoline_load.c:151` | precedent, not the thing |
| Image knows its own extent | yes, page-aligned, three boards; UEFI reads PE `SizeOfImage` | `metal/port/boards/X86_64_BIOS/link.ld:8`, `:66`; `metal/src/.../boot/__impl__.c:83`; `main.c:51` | **have** |
| Object directory (id → type, location, size, align, generation) | **nothing**. Searches for `object_id`, `obj_table`, `handle_table`, `objdir` return zero hits in `metal/src`, `wasmmod/src`, `wasmmod/ports` | — | **missing** |
| Stable id + generation handle discipline | yes, for registry modules: `{index, generation}`, bump on slot reuse, one validating chokepoint | `wasmmod/src/.../registry/__types__.h:42`; `__impl__.rs:1162`, `:1220` | **have** (as pattern) |
| Runtime type catalog with layout | yes: kind, instance size, fqn, parent, fields with offsets; runtime registration; full copy-out introspection | `wasmmod/src/pymergetic/types/__types__.h:109`, `:86`; `__impl__.c:985`, `:1031`, `:1063` | **have**, needs ids + align |
| Materialized allocator initial state | **no**. No snapshot, freeze, arena image, or serialize anywhere | searched `metal/src`, `metal/port`, `wasmmod/src`, `wasmmod/ports` | **missing** |
| Allocator state lives inside the span (imageable at all) | yes: header at front of caller's region, pools carved from the same span | `wasmmod/src/pymergetic/util/mem/__impl__.c:84-130` | **have** |
| Arena enumeration (to write an image) | `tlsf_walk_pool` / `tlsf_get_pool` / `tlsf_check_pool` vendored, **zero callers**; card face is scalar stats only | `wasmmod/third_party/tlsf/tlsf.h:80`; `util/mem/__exports__.h:16-43` | one small face away |
| Position-independent heap | **no**: absolute pointers in the arena header and in every TLSF block link and free-list head | `util/mem/__impl__.c:26-35`; `third_party/tlsf/tlsf.c:303`, `:346` | **missing**, see `03` |
| Semantic records as authority | **no**. Authority is embedded text, 25 MB, compiled against on-disk include paths | `metal/src/.../inspect/src_embed.inc.h` (88 cards); consumed by `build/__impl__.c:2189` | **missing**, see `05` |
| A real AST for some language | yes, two: rsx (48 kinds) and cppx (27), both lossy one-way lowerings with `#line` provenance | `metal/src/.../jit/rs/compiler/__types__.h:160`, `:108`; `jit/cpp/__types__.h:131` | partial |
| C parsed representation | span-addressed only: 2 kinds (fn, define), no ids, no nesting, no re-emitter | `metal/src/.../edit/__types__.h:50`, `:56`; `__impl__.c:344` | partial |
| Re-emit source from a representation | **nothing** turns a parsed C representation back into C text | — | **missing** |
| Id-based references | **no**. In-kernel link resolves names via `dlopen(NULL)`+`dlsym`, `/proc/self/maps`, `MAP_32BIT` thunks | `metal/src/.../build/__impl__.c:1389`, `:1325`, `:1434` | **missing**, see `04` |
| Bounded relocation table | precedent: ELF `RELA` walker with TLS materialization | `wasmmod/src/.../pack/format/elf/load.c:728`, `:1554` | precedent |
| Path namespace, one identity | four disjoint namespaces; fs card is a flat string list with a hardcoded FAT fall-through | `metal/src/.../fs/__impl__.c:9`, `:52`, `:158` | **missing** |
| Pluggable path backend in kernel | **none**. No ops table, no mount list. `GenSink` has the right shape but is host-side Rust | `wasmmod/src/pymergetic/util/gen/sink.rs:11` | **missing** |
| External mount of an un-started artifact | yes, for packs: open by host path, path-addressed, no instantiation | `wasmmod/src/.../pack/source.h:69-81` | **have** (as pattern) |
| Enumerable hierarchy (readdir) | yes for pack source (`list_files`/`list_modules`/`list_submodules`); **no** for fs, inspect, or the registry | `pack/source.h:86-105` | pattern only |
| Generated view with provenance | yes: `/docs/<fqn>/<fn>` renders from embedded source and reports file + line | `metal/src/.../inspect/__impl__.c:1322`, `:948` | **have** |
| Writable view | **no**. Only mutating route is `POST /build` (rebuild), which takes no bytes | `inspect/__impl__.c:3118-3228` | **missing** |
| Contained compiler | yes: TCC in-image on all four boards, four target lanes, arena-backed | `metal/src/.../jit/c/__types__.h:68`; `metal/port/fw_tcc.mk` | **have** |
| Contained linker | **host and unix µPy only**; boards refuse | `metal/Makefile:176`, `metal/metal.mk:121`; refusal at `build/__impl__.c:1599` | **partial — the big gap** |
| Compiler refuses instead of dying | **no**. Upstream reallocator `exit(1)`; our arena reallocator returns NULL and `tcc_mallocz` memsets it | `metal/externals/tcc/libtcc.c:258`, `:294-311`; `jit/c/__impl__.c:44-81` | **missing** (channel exists: `libtcc.c:697`, armed at `:814`) |
| Boot checker | **nothing**. Acceptance today is "it linked" | — | **missing** |
| Journal / prepare / commit / recovery | **nothing**: no journal, no WAL, no A/B slot, no atomic commit, no superblock, no checksum over a persisted record | searched `metal/src`, `metal/port`, `tools` | **missing** |
| Durable write of any kind | block write works on firmware; **only three callers, all unit tests**. FAT read-only. One `fopen("wb")` is a unix-only debug mirror, off by default, never read back | `drivers/blk/virtio/__impl__.c:236`; `fs/__fat__.c` (no `write`); `workspace/__impl__.c:83` | **missing** |
| Chainload a new image | **nothing**. No kexec, no UEFI `LoadImage`; only `pm_metal_process_reboot` | — | **missing** |
| Dynamic capacity everywhere | yes: knobs with soft/hard/default/used, per card, runtime-movable from C, C++, Rust, Python | `wasmmod/src/pymergetic/util/limits/` | **have** |
| Trust separation (generator vs gate) | **no**. Build actor and seat are one trust domain in one process | `build/__impl__.c:4011` | **missing** |

## Measured numbers

Board undefined-symbol surface, across the 79 card objects in
`metal/port/build/X86_64_BIOS-mp-repl/cards`:

| | Count |
|---|---|
| distinct undefined symbols | 512 |
| `pm_*` | 332 |
| `mbedtls_*` | 70 |
| libc / TCC / board glue | 110 |
| of the 332 `pm_*`: registry-exported | 211 |
| of the 332 `pm_*`: **not** exported | **121** |
| `PM_MOD_EXPORT_C` names across both source trees | 652 |

Per-card undefined counts: console 10, edit 18, net/ip 44, build 47.

In-kernel rebuild, from `metal/build/ksweep_report.txt`:

| | Value |
|---|---|
| cards discovered / compiled in-kernel | 86 / 86 |
| refused / not-buildable | 0 / 0 |
| exported symbols across linked images | 1153 |
| largest single image | `pymergetic.metal.build`, 5,509,120 B, 37 syms, 1063 ms |
| sweep wall time | 4.1 s |

Embedded source table: 88 cards, 25,078,668 bytes, 340,285 lines
(`metal/src/.../inspect/src_embed.inc.h`). Discover yields 86 units because entries with
a null or unparseable `__pmm__.toml` are skipped — both numbers are correct for what
they count.

A whole BUILD ALL of this repo: 103 objects, 18.1 MiB, largest single object 5.6 MiB
(`metal/src/.../build/__types__.h:199`).

## Capacity limits that a state binary inherits or must replace

| Limit | Value | Where |
|---|---|---|
| registry module rows `MOD_MAX` | 128 | `registry/__impl__.rs:95` |
| registry fqn / version / sig / name caps | 64 / 32 / 256 / 64 | `registry/__impl__.rs:121`, `:122`, `:131`, `:133` |
| µPy bridge signature buffer | **160** — silently below the registry's 256 | `ports/micropython/nativecall.c:238`, `:278` |
| loader LOADED rows | 128 | `loader/__impl__.rs:688` |
| loader wasm adapter slots | 8 (exhaustion rolls the whole load back) | `loader/__impl__.rs:357` |
| ELF adapter slots | 32 (exhaustion publishes a **partial** export set, no refusal recorded) | `ports/micropython/packbind.c:472` |
| boot rows / dep edges | 64 / 128 | `wasmmod/src/.../boot/__impl__.c:7`, `:11` |
| build objects per unit `MAX_OBJS` | 16 (a recording cap, not a compile cap) | `metal/src/.../build/__types__.h:31` |
| build actor queue depth | 16, backpressure is a refusal | `build/__types__.h:265` |
| build records / symbols per record | 128 / 64 | `build/__types__.h:442`, `:444` |
| edit node knob default / name cap | 256 / 96 | `metal/edit/__types__.h:40`, `:56` |
| types registry pool / default | 64 / 512, **no removal path** | `types/__impl__.c:924-930` |
| µPy object handle slots | 32, **no generation counter** | `ports/micropython/objhandle.c:6` |

The last two are the ones a state binary must not inherit: a catalog that cannot forget
and a handle that cannot detect staleness.

## Boundaries of what selfhost proves

Proven: all 86 discovered card units compile in-kernel across four implementation
languages, and all 86 link in-kernel through the ELF relocator. The rsx compiler reaches
a byte-exact fixed point (gen-2 identical to gen-1, 2,093,197 bytes) and reaches it again
through the full kernel chain with no host C compiler — Rust → C → TCC → link → run.
The C++ card reaches its own fixed point and its linked image executes correctly.
Symbols are looked up by name out of a freshly linked image and invoked as function
pointers.

Not proven, and worth being precise about:

1. **Not on firmware.** Every stage needs `PM_METAL_BUILD_HAS_ELF`, host and unix µPy
   only.
2. **Stages 0 and 2 use the host C compiler.** The no-host-CC claim belongs to the
   in-kernel object/link stages, not to the loop as a whole.
3. **ksweep is a readiness map, not a gate.** A refusal there is data.
4. **Byte identity is not semantic correctness** and does not exclude a trusting-trust
   attack. A fixed point is one gate among several.
5. **Nothing proves a rebuilt artifact survives anything.** Every stage ends in an
   in-memory byte compare or an in-process call. No stage writes an image, no stage
   reboots, no stage reads a previously produced artifact back from any medium.
