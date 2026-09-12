# Realization plan

Two lanes. The **external toolchain** lane starts first and is fully specified in
`11-BOOTSTRAP-AND-QUORUM.md` (Phase A milestones: spine, inspector, FUSE, FTP). The stages
below pick up where that lane's spine holds: they are the artifact itself, and each one lands
with its own prove. A stage that works on the host seat and not on a board is not finished —
see `.cursor/rules/all-platforms.mdc`.

    10|
## 0 — Compiler refusal instead of death — DONE in-tree

The old plan's stage 2 is already complete and is recorded here as done, not future work: the
nomem escape (`tcc_set_nomem_jmp`, vendored `externals/tcc/libtcc.c:313`) is armed around every
compile path — native, arm, x64 and wasm — with `setjmp` at each entry
(`jit/c/__impl__.c`), and the prove exists in `jit/c/__tests__.c`: a squeezed arena produces a
loud typed refusal, the process survives, and a healthy arena still compiles afterwards. This
is the "nothing may die where it should refuse" non-negotiable, holding.

## 1 — The cell heap

`pymergetic.state`: the cell header, intrinsics, domains, bump frontiers, the directory
(id -> domain+offset, sorted), and the tree faces of `01` re-ranked under the face of `10`.
Reclaim is domain retirement; there is no free list anywhere. Capacity as knobs under
`pymergetic.state`, growth on demand, refusals that name the knob.

*Prove:* build a tree in a domain, walk it, resolve paths; assert a stale handle is refused and
never redirected; assert a listing is ordered and reproducible; assert a squeeze refuses the
next cell rather than corrupting one; assert the directory rebuild-from-spans equals the
recorded directory (this same function is the boot check). Determinism: the same tree on two
CI platforms yields identical bytes.

## 2 — Project the live system into the tree

No new state: populate a cell tree from what the running system already knows — the registry's
modules and exports, the type catalog's types and fields, the limits card's knobs, the fs
card's files, the embedded card sources as `Bytes` cells. Then serve `/state/...` over the
existing HTTP surface, next to `/src` and `/build`.

*Prove:* the tree's module list equals the registry's module list; every knob appears under its
owning card; `/state/modules/pymergetic/metal/net/ip/socket` and `limits.of(...)`
agree. The browser seat gets the same JSON as the host seat. This validates the object model
against 88 cards, 652 exports and ~90 knobs before any container exists.

## 3 — Declarations as records

Stage β of `05-SEMANTIC-AUTHORITY.md`: types, fields, signatures, exports and imports become
real records — sourced from the live catalogs, then owned by the tree. Needs stable type ids
(today identity is descriptor pointer equality, `types/__impl__.c:405`) and explicit alignment
on type records (today absent).

*Prove:* regenerate `types/__view__.h` from the records and compare byte for byte against the
live-registry version — the existing drift check gives this for free. Then compile a card
in-kernel whose declarations come from records instead of headers, on every seat that has a
compiler.

## 4 — Id-based references and the imports section

Reference records as `{from, from_slot, to, to_off, kind, binding}`, an `IMPORT` cell per
genuinely external symbol, and a resolver that uses the directory and section mapping instead
of `dlopen`/`dlsym`/`/proc/self/maps`.

*Prove:* **link a card in-kernel on a firmware board.** That single assertion is the whole
point: today the boards refuse with "link: no loader on this seat"
(`build/__impl__.c:1599`). Secondary: the import list matches the measured 180 external
symbols, and a missing mandatory import is a loud refusal naming the symbol.

## 5 — The container

Header, section table, mapping kinds, entry record, a writer, a reader, and the boot checker.
Write an artifact holding a trivial state (one type, one function, one live object, one
domain) and mount it from outside without starting it — through every channel bound at that
stage, per `10`.

*Prove:* round-trip — write, mount externally, read every node, compare against the source
tree; then assert the boot checker rejects each of a set of deliberately broken candidates
(overlapping domains, unresolvable mandatory reference, out-of-range relocation, missing
target variant, unreachable entry). The reader works on every seat, including the browser.

## 6 — Boot from the artifact

The COPY spans per domain, the resource binding, the checks (the stage-1 directory rebuild),
and the hand-over. First allocation after boot bumps a restored frontier — no scan, no free
structure to materialize, because none exists (`03`).

*Prove:* boot a board from an artifact, and assert the first `alloc` lands where the recorded
frontier says, measured via the directory *before* the first allocation, and by timing. The
filing's counter example is the first payload: a live object at generation 41, incremented on
the first call.

## 7 — Durability and the journal

A durable writer over the block device (the capability exists on firmware,
`drivers/blk/virtio/__impl__.c:236`, with no callers outside unit tests), then the journal:
prepare record with sequence and integrity tag, safe point, atomic root switch, commit marker,
idempotent recovery. The build ledger becomes its first real client instead of a RAM copy
seeded from `.rodata`.

*Prove:* write a note, reboot, read it back. Then power-loss injection: kill the machine at each
of the three windows (before prepare, after prepare and before commit, after commit) and
assert recovery picks exactly one generation every time. This is the stage that needs a QEMU
harness rather than a unit test.

## 8 — Successor generation

Delta, dependency closure, joint rewrite, candidate, boot check, activation. Then the loop:
change one constant through a view, produce a successor, activate it, observe the new
behaviour, and roll back.

*Prove:* the filing's own worked example — change an increment from 1 to 2 through a generated
view, assert the closure covers the function, its caller, the new code body, the code mapping,
the reference, the directory entry, the domain occupancy and the entry path; assert the
candidate is refused when the base generation is stale; assert the accepted successor boots and
the first increment adds 2.

## 9 — The flip

Port the reader/checker inward against the frozen face and schema, then the view generator,
then the formatter — each gated by its byte-exact fixed point against the external tool
(`11`'s ladder). The formatter row is the flip: from there, every change to the artifact is
authored as a view write -> delta -> successor, including changes to its own type records.

*Prove:* the ladder's fixed points, on unix first (mmap + the in-tree ELF relocator, no board
bring-up in the loop), then the firmware seats with only the channel fills differing. Then:
`stbn replay --journal` re-derives the running artifact byte-for-byte (the certification
invariant of `11`), and the quorum disagrees loudly on any divergence.

## Structural work that runs alongside

**Trust separation.** The variant generator writes only to staging, holds no active-registry
publish face, and holds neither commit key nor commit capability; only a separated acceptance
gate mints activation evidence. On hosted seats the gate reads the candidate over the HTTP
channel like any other client — network separation is the honest kind (`10`). Today the build
actor and the seat are one trust domain in one process; this is a design task to start early,
because it constrains stages 7 and 8 rather than following them.

**Per-transaction limit views.** The plasmid's change contract carries resource ceilings as an
executable control object. The limits card is nearly all of that already; what is missing is
scoping a knob to one transaction instead of setting it globally.

**The conformance corpus.** Born with the spine (`11` Phase A) and extended by every stage:
same-path byte equality across channels, the schema dump between the two primitive spellings,
cross-platform seed determinism, and — from stage 5 on — the corpus runs against real artifacts,
not just the seed.

## Carried-forward defects worth fixing on the way past

- The host `Makefile` has no header dependency tracking at all — no `-MMD`, no `-MP`, no
  `.d` inclusion — while every board has it (`port/boards/X86_64_UEFI/build.mk:196`). Fix
  before stage 3, or record-sourced declarations will appear to work while stale objects
  linger.
- `MP_WASM_ELF_n_SLOTS = 32` static adapter thunks, and the ELF path publishes a partial
  export set on exhaustion without recording a refusal (`ports/micropython/packbind.c:472`).
  Stage 4 territory.
- `process.budget_set` refuses `cap <= 0` while knobs treat 0 as unlimited
  (`metal/process/__impl__.c`) — an inconsistency in the one place that already maps an id to
  a memory region.
- The µPy bridge reads signatures into a 160-byte buffer while the registry stores up to 256
  (`ports/micropython/nativecall.c:238`), so a long signature is stored fine and silently
  unreadable from Python.
- `pm_wasmmod_registry_*` and `loader.load` are not callable from Python; the type registry has
  no removal path; `MOD_MAX = 128` is shared by registry module rows and loader LOADED rows.