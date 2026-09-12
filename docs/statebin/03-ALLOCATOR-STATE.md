# The materialized allocator state

The claim: after the initial state is established, a first allocation runs *without a scan or a
reconstruction* of the occupancy and free structures. That is what separates this from a snapshot
that has to be thawed.

## No allocator state to image — by construction

The previous revision of this doc treated imaging TLSF as the central obstacle and weighed three
ways out (fixed base, reseed, offset links). The cell model removes the obstacle instead of
solving it, and the reason is worth stating exactly.

TLSF has free lists because its clients hold **raw pointers that must never move**. The artifact
has no such clients: every reference is a node id (`04-REFERENCES.md`), and a running instance
derives addresses from the section mapping plus the directory. Id-referenced objects are
**compactable**, and a compactable heap needs no free lists at all:

- allocation is a frontier bump inside a *domain* (below);
- reclaim is domain retirement — an epoch drops a whole domain, never a per-object free;
- boot restores per-domain frontiers by copying spans. The first allocation works with zero
  reconstructed structure, not because a free list was imaged perfectly, but because there is
  no free list.

The old three-option fork therefore dissolves. A fixed base (`want_base`) survives only as a
*relocation* question for executable code spans, not as a serialization question; the object
memory is position-independent by construction because no allocator-internal pointers exist
anywhere.

## The cell: one common object type

Every byte of object memory is inside a cell, and the header is simultaneously allocator
metadata, directory entry, and type-catalog back-pointer — three subsystems collapsed into one
24-byte record (`01-OBJECT-MODEL.md` has the model half):

```c
typedef struct pm_state_cell {     /* header; the payload follows */
    uint32_t id;        /* stable identity; never an index; 0 invalid, 1 = root */
    uint32_t type;      /* id of a TYPE cell; 1..31 are the intrinsics */
    uint32_t gen;       /* content epoch; bumped when this cell's content changes */
    uint32_t flags;     /* mutable / sticky / raw / retired / split */
    uint32_t size;      /* payload bytes following this header */
    uint32_t parent;    /* tree link: the owning cell */
} pm_state_cell_t;      /* 24 bytes; canonical field order; digest over header+payload */
```

Deliberate choices:

- **Tree links live in the parent's payload, not in the header.** A container cell's payload is
  its ordered directory — an array of `{ child_id, name_off }` entries — which is both the child
  list and the path-resolution structure (`child_named` scans a warm cache line). It is also why
  a subtree sits contiguously in address space. Inserting a child rewrites the parent's
  directory and never a sibling's record, so a successor still takes unchanged cells byte for
  byte (`07-SUCCESSOR-AND-ACTIVATION.md`).
- **Forced typing: "raw" is a type, not an absence.** `Bytes` is an intrinsic. No untyped
  allocation exists, so a walk can interpret every span and a view can render every object.
- **References are payload, never magic.** A `Ref` cell carries `{ target, sub_off, kind,
  binding }` — early-bound, the hard link. A `Path` cell carries a name resolved through the
  path records at bind time and **refused loudly when dangling** — late-bound, the symlink.
  Only genuinely external names survive as `IMPORT` records (`04`).
- **Large payloads split.** A header cell with `flags |= split` points into a dedicated large
  span, so a 5 MB code body does not disturb domain bumping. Code bodies are split cells in
  executable spans by construction.

## Domains: proximity made precise

A domain is a contiguous sub-span of an arena with one bump frontier and one epoch. "Proximity"
is then three concrete properties:

1. **Allocation-time**: children allocate in the parent's domain; a subtree is physically
   contiguous, and listing `/modules/pymergetic/metal/...` touches a handful of cache lines.
2. **Delta-time — the one that pays for the whole design**: a semantic change rewrites *one*
   domain. Unchanged domains are byte-identical, so the successor formatter's "take unchanged
   regions byte for byte" (`07`) is not a trick the formatter performs; it is the default
   outcome of locality.
3. **Boot-time**: `COPY` sections correspond to domains; what gets copied is exactly what is
   live and mutable.

Domains are created by the tree (a card gets a domain, an arena gets one), grown by extending
into the arena's free tail, and retired by epoch: a dead generation's domain is dropped
wholesale, never swept object by object. Intra-domain slide compaction (denser, more machinery)
is deliberately **not** in the first cut — epoch retirement alone is the v1 reclaim story.

## What the file holds, and what boot checks

Per arena: its span and align, and the domain set — each domain with `{ span, align, frontier,
epoch }`. The directory (`id -> (domain, offset)`, sorted, binary-searchable). A per-domain
digest manifest: every cell's content digest, in id order.

The directory is *rebuildable from the spans themselves* by walking cell headers at stride, and
that rebuild-and-compare **is the boot check** — verification, not reconstruction:

- every cell lies inside its domain's `[start, frontier)`; no domain overlaps another;
- every directory entry matches a header at the recorded offset with the same id;
- every header's type resolves to a TYPE record; every payload parses as that type says;
- every `Ref` resolves to a live id; every mandatory cell named by a reference record or an
  entry record exists;
- every occupied interval satisfies its type's alignment; every split cell's span bounds hold;
- the per-domain digest manifest matches the copied bytes.

A failure is the failure path from `02-CONTAINER.md`, never a partial hand-over. The same checks
run before a successor is accepted (`07`) — one function, two callers.

## Alignment, and the wrinkle worth keeping

TYPE records carry alignment explicitly (the runtime catalog lacks it today,
`types/__types__.h:109`), and a cell honors its type's align inside its domain; the boot check
verifies it rather than assuming it. Code spans keep the harder rule the tree learned the hard
way — 16-byte (`max_align_t`) alignment for anything TCC-shaped, after the jit card's observed
`block()` SIGSEGV on misaligned arena blocks (`jit/c/__impl__.c:44-68`) — and code domains are
aligned accordingly.

## Relation to the tree's TLSF

Nothing here touches `pymergetic.util.mem`: TLSF remains the allocator for host-side card
memory and for everything outside artifact spans. The old plan's first stage — exposing
`arena_walk` over `tlsf_walk_pool` as the imager's input — is superseded: walking cells is a
pointer loop over headers, and the artifact needs no TLSF walker. The vendored walker stays
what it is: linked, unreferenced.