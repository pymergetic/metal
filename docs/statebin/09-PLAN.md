# Realization plan

Ten stages. Each one lands on every seat in the same change and carries its own prove:
host C (`make -C extmod/metal test`), unix µPy (`make upy`), emcc browser
(`make browser`), all four firmware boards (`make firmware-prove`), and the wasmmod-only
unix µPy seat in `packages/micropython-wasmmod` where the card is shared. A stage that
works on the host seat and not on a board is not finished — see
`.cursor/rules/all-platforms.mdc`.

Stages 1 through 4 are independently useful even if the rest never happens. Stage 5 is
the one that changes what the system fundamentally is.

## 1 — Arena enumeration

Expose a walk and a check on `pymergetic.util.mem`: `arena_walk` with a callback over
`(offset, len, used, node)`, and `arena_check` over `tlsf_check_pool`. The vendored
walker already exists with zero callers (`third_party/tlsf/tlsf.h:80`).

*Prove:* allocate a known pattern, walk it, assert the intervals sum to the span with no
overlap; assert an arena grown with `add_pool` reports its spare pools or refuses. Same
test on every seat — this is pure card work, so it should pass identically everywhere.

*Why first:* it is the input to the imager, it is small, and it is useful on its own for
diagnosing the arena pressure the boards already live with.

## 2 — Compiler refusal instead of death

Convert TCC's out-of-room paths into typed refusals using the `setjmp`/`longjmp` channel
that is already armed for the whole of `tcc_compile` (`externals/tcc/libtcc.c:697`,
`:814`). Today the upstream reallocator calls `exit(1)` (`:258`) and our arena
reallocator's honest NULL gets `memset` by `tcc_mallocz` (`:304-311`).

*Prove:* squeeze the jit arena knob to a value that cannot hold a known compile, run it,
and assert a typed refusal naming the knob — with the process still alive and a second
compile at the normal knob still succeeding. On firmware this matters most, because the
board's whole free map may be a few megabytes.

*Why second:* nothing downstream can be trusted to fail safely until this holds. It is
also a prerequisite for the plasmid loop, where a variant build must not be able to kill
the parent generation.

## 3 — The node table

`pymergetic.state`: node records, id space, kinds, name table, parent/child links,
`{id, generation}` handles with a single validating chokepoint, and the tree faces
(`root`, `parent`, `child_count`, `child_at`, `child_named`, `by_id`, `by_path`, `path`).
Capacity as knobs under `pymergetic.state`, growth on demand, refusals that name the
knob.

*Prove:* build a tree in an arena, walk it, resolve paths, assert a stale handle is
refused and never redirected, assert a listing is ordered and reproducible, assert a
squeeze refuses the next node rather than corrupting one. Python face too, since the
knob work established that every card face is reachable from all four languages.

## 4 — Project the live system into the tree

No new state: populate a node tree from what the running system already knows — the
registry's modules and exports, the type catalog's types and fields, the limits card's
knobs, the fs card's files, the embedded card sources as `BLOB` nodes. Then serve
`/state/...` from it over the existing HTTP surface, next to `/src` and `/build`.

*Prove:* the tree's module list equals the registry's module list; every knob appears
under its owning card; `/state/modules/pymergetic/metal/net/ip/socket` and
`limits.of("pymergetic.metal.net.ip")` agree. Browser seat gets the same JSON as the
host seat.

*Why:* this is the first thing a person can look at. It also validates the object model
against 88 cards, 652 exports and ~90 knobs before any file format exists.

## 5 — Declarations as records

Types, fields, signatures, exports and imports become real records rather than
projections — sourced from the live catalogs, then owned by the tree. This is stage β of
`05-SEMANTIC-AUTHORITY.md`, and the first point at which the artifact answers layout and
interface questions without parsing anything.

Needs: stable type ids (today identity is descriptor pointer equality,
`types/__impl__.c:405`) and explicit alignment on type records (today absent).

*Prove:* regenerate `types/__view__.h` from the records and compare byte for byte against
the version generated from the live registry — the existing drift check gives this for
free. Then compile a card in-kernel whose declarations come from records instead of
headers, on every seat that has a compiler.

## 6 — Id-based references and the imports section

Reference records as `{from, from_slot, to, to_off, kind, binding}`, an `IMPORT` node per
genuinely external symbol, and a resolver that uses the node table and section mapping
instead of `dlopen`/`dlsym`/`/proc/self/maps`.

*Prove:* **link a card in-kernel on a firmware board.** That single assertion is the
whole point of the stage, because today the boards refuse with "link: no loader on this
seat" (`build/__impl__.c:1599`). Secondary: assert the import list for a board build
matches the measured 180 external symbols, and that a missing mandatory import is a loud
refusal naming the symbol.

*Why here:* this is the highest-value stage in the plan. It converts in-kernel rebuild
from a host-only capability into a property of every seat.

## 7 — The container

Header, section table, mapping kinds, entry record, a writer, a reader, and the boot
checker. Write an artifact holding a trivial state (one type, one function, one live
object, one arena) and mount it from outside without starting it.

*Prove:* round-trip — write, mount externally, read every node, compare against the
source tree; then assert the boot checker rejects each of a set of deliberately broken
candidates (overlapping objects, unresolvable mandatory reference, out-of-range
relocation, missing target variant, unreachable entry). The reader must work on every
seat, including the browser.

## 8 — Boot from the artifact

The arena image (option A of `03-ALLOCATOR-STATE.md`: fixed base, no fixups), the copy
and map sections, the resource binding, the checks, and the hand-over. First allocation
after boot uses the materialized free structure.

*Prove:* boot a board from an artifact, and assert the first `alloc` succeeded with no
scan — measured, by asserting the walk from stage 1 reports the expected intervals
*before* the first allocation, and by timing. The counter example from the filing is a
good first payload: a live object at generation 41, incremented on the first call.

## 9 — Durability and the journal

A durable writer over the block device (the capability exists on firmware,
`drivers/blk/virtio/__impl__.c:236`, with no callers outside unit tests), then the
journal: prepare record with sequence and integrity tag, safe point, atomic root switch,
commit marker, idempotent recovery. The build ledger becomes its first real client
instead of a RAM copy seeded from `.rodata`.

*Prove:* write a note, reboot, read it back. Then power-loss injection: kill the machine
at each of the three windows (before prepare, after prepare and before commit, after
commit) and assert recovery picks exactly one generation every time. This is the stage
that needs a QEMU harness rather than a unit test.

## 10 — Successor generation

Delta, dependency closure, joint rewrite, candidate, boot check, activation. Then the
loop: change one constant through a view, produce a successor, activate it, observe the
new behaviour, and roll back.

*Prove:* the filing's own worked example — change an increment from 1 to 2 through a
generated view, assert the closure covers the function, its caller, the new code body,
the code mapping, the reference, the object directory entry, the allocator occupancy and
the entry path; assert the candidate is refused when the base generation is stale; assert
the accepted successor boots and the first increment adds 2.

## Structural work that runs alongside

**Trust separation.** The variant generator must write only to staging, hold no
active-registry publish face, and hold neither commit key nor commit capability; only a
separated acceptance gate can mint activation evidence. Today the build actor and the
seat are one trust domain in one process. This is a design task to start early, because
it constrains stages 9 and 10 rather than following them.

**Per-transaction limit views.** The plasmid's change contract carries resource ceilings
as an executable control object. The limits card is nearly all of that already; what is
missing is scoping a knob to one transaction instead of setting it globally.

## Carried-forward defects worth fixing on the way past

These are known, reported, and not yet done. Each is small, and each one intersects a
stage above:

- The host `Makefile` has no header dependency tracking at all — no `-MMD`, no `-MP`, no
  `.d` inclusion — while every board has it (`port/boards/X86_64_UEFI/build.mk:196`).
  Fix before stage 5, or record-sourced declarations will appear to work while stale
  objects linger.
- `MP_WASM_ELF_n_SLOTS = 32` static adapter thunks, and the ELF path publishes a partial
  export set on exhaustion without recording a refusal
  (`ports/micropython/packbind.c:472`). Stage 6 territory.
- `process.budget_set` refuses `cap <= 0` while knobs treat 0 as unlimited
  (`metal/process/__impl__.c`) — an inconsistency in the one place that already maps an
  id to a memory region.
- The µPy bridge reads signatures into a 160-byte buffer while the registry stores up to
  256 (`ports/micropython/nativecall.c:238`), so a long signature is stored fine and
  silently unreadable from Python.
- `pm_wasmmod_registry_*` and `loader.load` are not callable from Python; the type
  registry has no removal path; `MOD_MAX = 128` is shared by registry module rows and
  loader LOADED rows.
