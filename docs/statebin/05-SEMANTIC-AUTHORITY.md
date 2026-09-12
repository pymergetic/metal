# Semantic authority

The claim that decides everything else: the records inside the artifact *are* the
meaning of the program. Reconstructing semantics and producing executable code needs no
separate leading source tree. A generated source view may be reformatted, may have lost
comments, and is explicitly **not** the authority.

This is the hardest part of the design and the one with the most lever, because it also
removes a problem the tree already has.

## The problem it removes

Today the authority is text. `tools/embed_src.py` puts every card's authored `.c` /
`.rs` / `.py` bytes into a 25 MB generated table (`inspect/src_embed.inc.h`, 88 cards),
and the build card reads that table as its authoritative unit list
(`build/__impl__.c:2189`). An in-kernel rebuild then compiles those bytes — against
include paths. The sweep has to hand TCC nine include directories and a set of defines
mirroring the host build (`tools/ksweep.c`, the include fill), because the sources
`#include` real headers: µPy's, mbedtls', TCC's, the board port's.

A board has the sources in its image but not the headers. That is why the firmware
seats can only compile self-contained translation units, and it is a direct consequence
of text being the authority.

If the authority is a semantic graph — types, fields, signatures, control flow, data
flow, effects — then include paths stop existing as a concept. There is nothing to
resolve, because there is no text to preprocess. A `FN` node's meaning is in its record;
a `CODE` child is one target's compiled body of that meaning. The artifact needs no
filesystem, no include search, and no header dependency tracking, because none of those
are inputs any more.

## What a semantic record has to hold

Per the claim: control flow, data dependencies, type rules, memory and lifetime rules,
side effects, error paths, external interfaces, and the mapping to executable code —
with every semantic node addressed by a stable node id.

```c
typedef struct pm_state_fn_rec {     /* payload of an FN node */
    uint32_t signature;      /* node id of a signature record */
    uint32_t n_ops;          /* operations, in a canonical order */
    uint32_t ops;            /* offset into the semantics section */
    uint32_t n_edges;        /* control edges + data edges */
    uint32_t edges;
    uint32_t effects;        /* memory writes, io, allocation */
    uint32_t error_fn;       /* node id of the error path, 0 = none */
    uint32_t view_hint;      /* node id of the preferred view generator */
} pm_state_fn_rec_t;

typedef struct pm_state_op {
    uint32_t id;             /* node id: an operation is an object too */
    uint16_t op;             /* semantic operation code */
    uint16_t n_in;
    uint32_t in;             /* operand node ids */
    uint32_t out;            /* result node id */
    uint32_t type;           /* result type node id */
    uint32_t origin;         /* position in the generating view, for diagnostics */
} pm_state_op_t;
```

An operation carries a node id, because a view write-back has to be able to say "the
constant in operation 0x1003:2 changed from 1 to 2" and have that survive as an
identity across the successor generation.

## What the tree has, honestly

Three representations exist, and none of them is this.

**`pymergetic.types` is a real runtime type catalog, and it is the strongest starting
point.** Descriptors carry kind, instance size, name, fqn, parent for inheritance,
field count and a field array; a field carries name, name hash, byte offset and field
type (`types/__types__.h:109`, `:86`). Registration is a live face —
`pm_types_registry_register` does a sorted insert keyed by fqn, refusing a different
descriptor under a live fqn (`types/__impl__.c:985`). And there is already a complete
copy-out introspection pair, `registry_type_at` and `registry_field_at`
(`types/__impl__.c:1031`, `:1063`), which is enough to dump the whole catalog as
records. Proof that the catalog is rich enough: `types/__view__.h` is generated from
the live registry and emits real C structs with explicit padding.

Three things it lacks for our purpose: type identity is a **descriptor pointer**, not a
stable id (`is_instance_of` walks the parent chain comparing pointers,
`types/__impl__.c:405`), which cannot be written to a file; descriptors carry **no
alignment**; and the registry has **no removal path** — a staged row lives as long as
the process, by the same contract as the arena its values come from.

**The `edit` card is span-addressed, and its own header says so.** `parse_c` does not
produce an AST. It produces a flat array of byte-span descriptors for exactly two
top-level shapes, `PM_METAL_EDIT_FN` and `PM_METAL_EDIT_DEFINE`
(`metal/edit/__types__.h:50`), each carrying name, name offset, span start, span end,
body offset, value offset and line. There is no node id, no nesting, no parent, and
`pm_metal_edit_locate` is a linear scan by `(kind, name)` (`edit/__impl__.c:344`). Both
mutators work by splicing bytes over the original source and returning a new buffer,
which invalidates every offset in the tree that produced it. **Nothing anywhere can
re-emit C text from a parsed representation.** The card is honest about this — the
comment at `edit/__types__.h:9` says libtcc exposes no AST and this is the honest AST
libtcc can support.

**rsx is a real AST, and it is the closest thing to a semantic pipeline.** The
Rust-to-C compiler (`metal/jit/rs/compiler/`, ~36,800 lines authored as eleven ordered
`parts/`) has a genuine node type — kind, line, text, kids, `int_val`, `op_kind` over 48
kinds (`jit/rs/compiler/__types__.h:160`, `:108`) — a lexer, a parser, a lowering to C
text, and an `ast_dump` that prints kind + text + line per node. `jit/cpp` mirrors the
same shape for a C++ subset (27 kinds).

Its limits for our purpose are equally clear. The node carries `line` only — **no byte
offset, no column, no end position** — so it cannot address a span the way the edit
card can. The lowering is deliberately lossy and one-directional: methods flatten to
free functions, `match` becomes an if/else chain, slices lose their length, tuples
become anonymous structs, and `use`/`mod` lower to comments (enumerated at
`parts/head.rs:49-108`). What survives is `#line` provenance and, crucially,
**determinism** — the same tree always yields the same bytes, proven by the byte-exact
self-host fixed point.

So the tree has: a type catalog that is nearly a types section, a real AST for two
languages that is one-directionally lowered, and a span editor that cannot re-emit.
What it does not have is a representation that is the *authority*.

## A staged path, because the jump is too big

**Stage α — text as an object, authority unchanged.** Embedded card sources become
`BLOB` nodes under `CARD` nodes rather than a generated C table. Nothing about meaning
changes; what changes is that source has an id, a generation, a digest, and a place in
the tree. This alone makes `/src/<fqn>/<file>` a tree walk instead of a special route,
and makes a source edit a node generation bump. Cheap, and it is the migration step for
25 MB of existing embedded bytes.

**Stage β — declarations become records, bodies stay text.** Types, fields, function
signatures, exports and imports become real records — sourced from the live catalogs
that already exist (`pymergetic.types` for types and fields, the registry for exports
and signatures). Bodies remain `BLOB` text compiled by the contained compiler. At this
point the artifact can answer "what is the layout of this type", "what does this
function take and return", "who calls whom" from records, without parsing anything —
and `04-REFERENCES.md`'s id-based call edges become expressible for declarations.

**Stage γ — bodies become records for one language.** Pick the language where a real
AST already exists and the pipeline is already deterministic: rsx. An `FN` node's
payload becomes operations and edges; the C text becomes a *generated view* rather than
the input. This is where the include-path problem actually disappears, and it is also
where the first genuine "no leading source tree" claim becomes true for some subtree.

**Stage δ — the other languages.** C through a real parser (the edit card's span model
is not enough), C++ through cppx's AST. Both are large. Neither is a prerequisite for a
working artifact, as long as the boundary is explicit: a card whose body is still text
is marked as such, and the artifact does not claim semantic authority over it.

The honest framing is that stages α and β are weeks, γ is months, and δ is a project.
The artifact is useful and demonstrable at β.

## Views and write-back

A view is a deterministic projection of a subtree, and it carries its provenance in
band: the view id, the base state id, and the node ids and generations it was generated
from. A write is not a byte replacement — it is parsed with the same view definition,
mapped back onto those ids, validated, and turned into a delta.

The identity rules are the claim, and they are worth stating as invariants:

- A node that survives the edit **keeps its id**.
- A newly introduced node gets a **new id, unique within the state**.
- A deleted node is recorded in the delta as **an id that went away**, and its
  dependents are either updated through the closure or the change is refused.
- Reversibility is at the level of meaning and identity, not formatting or byte offsets.
  A regenerated view need not match the one that was read.

Concurrency is generation comparison, not locking: a write bound to base generation 12
of a node fails with the node id, the base generation and the current generation when
another transaction has moved it. Disjoint changes may merge after re-validation.

The precedents worth reusing: the edit card's three-gate write is already the right
order — a ledger note must predate the edit, the typecheck must pass, and only then is
the write applied (`edit/__impl__.c:495`). And `pymergetic.util.gen` already has the
sink abstraction for reading current bytes, comparing, and reporting
`Unchanged / Wrote / Drift / Missing` (`extmod/wasmmod/src/pymergetic/util/gen/sink.rs:170`),
which is the shape of an idempotent apply — though it lives host-side in Rust and
nothing under `metal/**` implements it.
