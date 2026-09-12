# Successor generation, boot check, and activation

Producing the next generation is four operations in a fixed order: a delta over node
ids, the dependency closure of those ids, one joint rewrite of everything in the
closure, and a boot check that gates acceptance. Then, and only then, activation.

## The delta

```c
typedef struct pm_state_delta {
    uint64_t base_state_id;
    uint32_t n_changed;      /* node ids whose content changed, with base generations */
    uint32_t n_added;        /* newly introduced node ids */
    uint32_t n_removed;      /* ids that went away */
    uint32_t closure;        /* the computed dependency closure (below) */
    uint32_t reason;         /* node id of the record that motivated the change */
} pm_state_delta_t;
```

A delta names ids, never byte ranges. It is bound to a base state id and to the base
generation of every node it touches, so a stale delta is refused with the node id, the
base generation and the current generation rather than applied to the wrong parent.

## The dependency closure

Given the changed ids, the closure is everything that must be rewritten with them:

- callers and callees of a changed `FN`
- type and field layouts of a changed `TYPE` — and every `DATA` node of that type
- reference records whose `from` or `to` is in the set
- the `CODE` bodies of changed functions, and the code/execution mapping
- object locations, if a body or object grew
- arena occupancy and free intervals, if any location moved
- the entry record and the boot section, if the entry or its code moved

The point of computing it is to make the failure it prevents impossible: a semantic
change that appears in one view or one code body while references, object locations,
allocator occupancy or entry data stay stale. That is not a hypothetical — it is the
exact failure mode the current fs card lives with, where a different-length rewrite
tombstones the old entry and leaks its bytes because nothing tracks who pointed at it
(`metal/fs/__impl__.c:136`).

## The successor formatter

Unchanged regions whose layout binding is still valid are taken **byte for byte**.
Everything in the closure is regenerated, and the following are updated *together* in
one pass: node records and generations, reference records, type and field layouts, arena
occupancy and free structures, code and execution mappings, path and view assignments
where affected, section bounds and digests, and the entry, relocation and resource
records.

"Together" is the requirement. A formatter that updates records in several independent
passes can be interrupted between them, and then the artifact is neither the old
generation nor the new one.

## The boot check

The successor is written as an **inactive candidate**. A separate checker decides whether
it may be called bootable:

- section bounds inside the file, no overlap
- every object location inside its section or arena, no overlap
- arena intervals cover their span exactly (`03-ALLOCATOR-STATE.md`)
- every mandatory reference resolvable
- every listed relocation admissible: section, offset, width, kind all in range
- the target code variant present for the declared target class
- the entry object reachable through the same ids and layout records boot will use
- the contained execution path complete, including the failure path
- the contained tools present

Only if all of them pass does the candidate get a valid state id. On any failure the
candidate stays inactive and the previous generation is untouched.

Today the entire acceptance test is "it linked." There is no checker.

## Activation

```
RECEIVED → VALIDATED → BUILT → CHECKED → STAGED
        → QUIESCING → ACTIVATING → ACCEPTED → OLD_RETIRED
```

with failures going to `REJECTED` before staging, `ROLLBACK_PENDING` after it, and a
restart going to `RECOVER_FROM_JOURNAL`.

1. Verify the activation evidence against node id, state ids, digests and the expected
   old and new generation.
2. Write a persistent prepare record: old root, candidate root, old and new generation,
   safe-point kind, sequence number, evidence digest, integrity tag.
3. Reach the safe point. New calls into the old generation are refused or marked;
   in-flight calls drain, are cancelled per contract, or migrate.
4. Switch the root or slot assignment atomically, idempotently repeatable.
5. Store the commit marker. **Only a valid commit marker makes the candidate accepted.**

On restart, recovery reads the prepare record, the highest valid sequence, the root
integrity and the commit marker, and picks exactly one action: keep the old generation,
finish the switch idempotently, or restore the old generation. A handle whose generation
no longer matches yields a typed stale refusal and is never redirected to new content.

## What exists and what does not

**The dispatch half of generation safety is already right.** The registry's handle is
`{index, generation}`, a reused slot bumps its generation, and one chokepoint validates
`live && generation == handle.generation` and returns NULL otherwise
(`extmod/wasmmod/src/pymergetic/wasmmod/registry/__impl__.rs:1162`, bump at `:1220`).
The loader indexes its own rows by the registry handle's index and refuses an unload
whose handle went stale, putting the row back rather than orphaning the buffer
(`wasmmod/loader/__impl__.rs:1742`). That is the stale-refusal discipline the state
model needs, already load-bearing in the tree.

**The persistent half does not exist at all.** No journal, no append-only log, no
double-buffered sector, no atomic commit, no superblock, no checksum over a persisted
record. `pm_metal_drivers_blk_write` works — the firmware virtio-blk path does real
descriptor-chain DMA (`drivers/blk/virtio/__impl__.c:236`) — and its only three callers
in the tree are the cards' own unit tests. FAT is read-only. Nothing can chainload a
newly written image.

**The ledger is journal-shaped but not durable.** `build/changes.jsonl` is 3 lines
authored by hand, embedded as bytes at make-parse time
(`build/changes_embed.inc.h`, via `tools/ledger.mk`), materialized into the fs card on
first use, and appended by read-modify-write against that RAM copy
(`build/__impl__.c:546`, `:601`). Architecturally it is an append-only log; physically it
is a monotonic RAM arena seeded from `.rodata`, so every note added at runtime is gone
at reboot. It is the right shape in the wrong medium — which makes it a good candidate
to become the first *real* journal client.

**There is no conditional publish.** The build card publishes a record unconditionally
once the link succeeds. What looks adjacent is not: `record_epoch` is a round-robin
eviction cursor, not a generation stamp; `s_walk_gate` is a single CAS that serializes
walk starts (`build/__impl__.c:5625`); `record_publish_locked` sets `valid = 1` after all
field writes so a reader sees a complete old or complete new record
(`build/__impl__.c:420`) — publication ordering, not a transaction.

**The build actor's two-stage locking is the right precedent for the write side.**
Stage one serializes the actor and parks a waiting job without blocking its runner;
stage two is a narrower allocator window that never spins, because TCC's reallocator is
a single process-global function pointer and two interleaved compiles on different
arenas would free each other's allocations (`build/__impl__.c:4011`, the window at
`jit/c/__impl__.c:200`). A journal writer plus a formatter needs the same layering: a
serialized transaction and a narrower window over the medium.

## Where the plasmid filing meets this one

The state binary is the artifact; the plasmid is the loop that decides to make a new
one. Two of its requirements are *structural* and constrain this design rather than
following it:

**The variant generator must be unable to publish.** It writes only to staging, has no
active-registry publish face bound, and holds neither the commit key nor the commit
capability. Only a technically separated acceptance gate — separate actor or process,
separate trust domain, separate commit capability, no shared writable active state, or a
canary measurement source the generator cannot write — can mint the activation evidence
that unlocks the root switch. Today the build actor and the seat are one trust domain in
one process, so this is a real split to design, not a flag.

**The change contract is an executable control object.** The plasmid's `MutationTask`
carries `max_ram`, `max_flash`, `max_cpu_time`, `max_energy`, `max_queue_depth`,
mutable and immutable fields, permitted imports and exports, a canary budget, and
acceptance and rollback predicates. The limits card is already most of that machinery —
soft, hard, default, live-used, per card, movable at runtime from all four languages.
What it cannot yet do is **scope** a knob to one build rather than setting it globally.
A per-transaction limit view is the piece to add, and it is a natural extension of the
knob tree rather than a new subsystem.
