# The object model

Everything is an object. There is one node table, one id space, and one way to reach
anything: start at the root and drill down.

## The node

```c
/* Provisional. Field order is canonical: the digest of a node record is taken over
 * these bytes in this order, little-endian, with no padding holes. */
typedef struct pm_state_node {
    uint32_t id;            /* stable within a state; 0 invalid, 1 = root */
    uint32_t generation;    /* bumped when this node's content changes */
    uint16_t kind;          /* pm_state_kind_t */
    uint16_t flags;
    uint32_t parent;        /* node id; root's parent is 0 */
    uint32_t first_child;   /* node id, 0 = leaf */
    uint32_t next_sibling;  /* node id, 0 = last */
    uint32_t name;          /* offset into the name section, 0 = anonymous */
    uint32_t type;          /* node id of a TYPE node, 0 = untyped */
    pm_state_loc_t loc;     /* where the bytes are (below) */
    uint32_t digest;        /* content digest of the bytes at loc, 0 = none */
} pm_state_node_t;
```

`first_child` / `next_sibling` rather than a child array, for one reason: inserting a
child must not move or resize a sibling's record, because a successor generation wants
to take unchanged records byte for byte (`07-SUCCESSOR-AND-ACTIVATION.md`). Children
are ordered, and the order is part of the canonical form — a listing is reproducible.

### Location

```c
#define PM_STATE_SPACE_FILE  0u   /* section-relative: bytes live in the artifact */
#define PM_STATE_SPACE_ARENA 1u   /* arena-relative: bytes live in a managed span */
#define PM_STATE_SPACE_NONE  2u   /* no bytes (namespaces, groups, pure semantics) */

typedef struct pm_state_loc {
    uint32_t space;     /* PM_STATE_SPACE_* */
    uint32_t where;     /* section id or arena node id */
    uint64_t off;       /* offset within that section or arena */
    uint64_t len;
    uint32_t align;
} pm_state_loc_t;
```

**No absolute address appears anywhere in the artifact.** A running instance derives
addresses from the section mapping plus `off`; the file never contains one. This is
the same `(space, offset)` shape the registry already uses for `pm_addr_t`
(`extmod/wasmmod/src/pymergetic/wasmmod/registry/__types__.h:107`), so the vocabulary
is not new to the tree — only its use as the *only* form of reference is.

### Identity, and why an id is not a slot index

A node id is stable for as long as the thing it names exists, across successor
generations. It is not an index into the node table and not a slot number: the table
is sorted by id for binary search, but a node that moves within the table keeps its
id.

A handle is `{id, generation}`, and a stale handle is refused, never silently
redirected:

```c
typedef struct pm_state_handle { uint32_t id; uint32_t generation; } pm_state_handle_t;
/* {0, 0} is the canonical invalid handle. */
```

This is deliberately the registry's discipline, transplanted. The registry's
`{index, generation}` pair with a bump on every slot reuse and a single validating
chokepoint (`registry/__impl__.rs:1162` for the check, `:1220` for the bump) is the
one place in the tree that already gets staleness right, and it is worth copying
exactly rather than reinventing. The difference is only that the registry's `index`
is a slot and ours is an identity.

Contrast the two places that get it *wrong* today, both of which a state binary must
not repeat: µPy object handles are bare 1-based slot indices with no generation
(`extmod/wasmmod/ports/micropython/objhandle.c:13`), so a released-then-reused slot
gives a stale handle silent access to the new occupant; and `pymergetic.types`
identifies a type by *descriptor pointer equality* (`types/__types__.h:128`,
`is_instance_of` at `types/__impl__.c:405`), which cannot survive being written to a
file at all.

### Names

`name` is an offset into a name section — one interned, NUL-terminated string table,
deduplicated, sorted for binary search. A node stores one component, never a path.

The full name of a node is the chain of components from the root. That is not a new
convention; it is the one the tree already uses for card fqns, and it is exactly the
shape the limits work landed on, where a knob carries its module and its leaf
separately and the joined form is derived (`src/pymergetic/util/limits/__types__.h:33`).
In the node model that split stops being special: the module is the parent, the leaf
is the child, and `pymergetic.metal.net.ip.socket` is a walk.

## Kinds

```c
typedef enum pm_state_kind {
    PM_STATE_ROOT = 0,
    PM_STATE_GROUP,      /* an unnamed-in-itself container: /types, /arenas, ... */
    PM_STATE_NAMESPACE,  /* a dotted component with no muscle: pymergetic, .metal */
    PM_STATE_CARD,       /* a module with muscle; flags carry the impl language */
    PM_STATE_TYPE,       /* a type record: kind, instance size, align, parent type */
    PM_STATE_FIELD,      /* child of TYPE: offset, type, mutability, lifetime rule */
    PM_STATE_FN,         /* a callable: signature, control/data flow, effects */
    PM_STATE_CODE,       /* child of FN: one target's code body */
    PM_STATE_DATA,       /* a live object in an arena */
    PM_STATE_ARENA,      /* a managed span with an initial state */
    PM_STATE_SECTION,    /* a region of the artifact */
    PM_STATE_LIMIT,      /* a capacity knob: soft, hard, default, used */
    PM_STATE_IMPORT,     /* a symbol this artifact needs from its seat */
    PM_STATE_EXPORT,     /* a name this artifact publishes */
    PM_STATE_VIEW,       /* a view generator */
    PM_STATE_TOOL,       /* a contained capability */
    PM_STATE_BLOB,       /* opaque bytes (an asset, an archive, embedded source) */
} pm_state_kind_t;
```

Kind-specific payload lives at `loc`, in a per-kind record laid out in a section — not
in the node record. A `TYPE` node's payload is a type record; an `FN` node's payload is
a semantic record; a `LIMIT` node's payload is the four numbers. The node record stays
one fixed size so the table is indexable and a successor can rewrite one node without
touching its neighbours.

The model is closed: the node table is a `SECTION` node, the name table is a `SECTION`
node, and both appear in the tree under `/sections`. There is no metadata that lives
outside the model looking down on it.

## The drill-down

```
1  ROOT                          "" (the state itself; carries the state id + format version)
├─ 2  GROUP      /sections       every region of this file, as nodes
│   ├─ SECTION   nodes           the node table itself
│   ├─ SECTION   names           the interned string table
│   ├─ SECTION   types           type + field records
│   ├─ SECTION   semantics       FN records
│   ├─ SECTION   code            target code bodies
│   ├─ SECTION   arena0          the materialized allocator image
│   └─ SECTION   journal         activation records
├─ 3  GROUP      /types          the type catalog
│   └─ TYPE      CounterState    instance_size, align, parent
│       └─ FIELD value           offset 0, type u32
├─ 4  GROUP      /modules        the card tree — the same fqns the registry reports
│   └─ NAMESPACE pymergetic
│       ├─ NAMESPACE metal
│       │   ├─ NAMESPACE net
│       │   │   └─ CARD  ip                       impl = c
│       │   │       ├─ LIMIT  socket              soft/hard/default/used
│       │   │       ├─ FN     pm_ip_socket_open
│       │   │       │   ├─ CODE  x86_64
│       │   │       │   └─ CODE  armv7
│       │   │       └─ EXPORT pm_ip_socket_open   published name + signature
│       │   └─ CARD  console
│       └─ NAMESPACE util
│           └─ CARD  limits
├─ 5  GROUP      /arenas
│   └─ ARENA     arena0          span, occupied/free intervals, first-alloc ready
│       └─ DATA  counter         live object, type CounterState, generation 41
├─ 6  GROUP      /imports        what the seat must provide (memcpy, mbedtls_*, ...)
├─ 7  GROUP      /tools          reader, view generator, closure, formatter, checker
├─ 8  GROUP      /views          view generators, by name
└─ 9  GROUP      /journal        prepare records and commit markers
```

A drill-down from the root reaches everything, in one id space, with one walk.

## The faces

```c
/* tree */
pm_state_handle_t pm_state_root(void);
pm_state_handle_t pm_state_parent(pm_state_handle_t node);
uint32_t          pm_state_child_count(pm_state_handle_t node);
pm_state_handle_t pm_state_child_at(pm_state_handle_t node, uint32_t n);
pm_state_handle_t pm_state_child_named(pm_state_handle_t node, const char *name);

/* identity */
uint16_t pm_state_kind(pm_state_handle_t node);
uint32_t pm_state_name(pm_state_handle_t node, char *buf, uint32_t cap);   /* one component */
uint32_t pm_state_path(pm_state_handle_t node, char *buf, uint32_t cap);   /* derived chain */
pm_state_handle_t pm_state_by_path(const char *path);                     /* walk, no table */
pm_state_handle_t pm_state_by_id(uint32_t id);                            /* binary search */

/* layout */
int32_t pm_state_loc_of(pm_state_handle_t node, pm_state_loc_t *out);
int32_t pm_state_bytes(pm_state_handle_t node, const uint8_t **out, uint64_t *len);
```

Two properties of that face matter more than the shapes:

**`pm_state_by_path` walks the tree; it does not consult a path table.** A name lookup
is `child_named` per component. That removes the class of bug where a path table and a
tree disagree — the failure mode the patent's shared identity base exists to prevent.

**Every accessor takes a handle, so every accessor can refuse.** A generation mismatch
is a typed refusal, the same as a squeezed knob refusing the next row rather than
corrupting one.

Today's tree walks are all O(all nodes) prefix rescans: the limits card compares
`k->module` against a prefix with a dot guard for every knob in a linked list
(`util/limits/__impl__.c:156`), the import hook rescans every live registry row to
find one module's children (`ports/micropython/importhook.c:234`), and the boot tree
counts three hardcoded families one level deep (`metal/boot/tree/__impl__.c:154`).
Those work because the counts are small, but none of them is a tree — they are
predicates over a flat list. `first_child` / `next_sibling` makes a listing O(children).

## What existing things become nodes

This is the migration map, and it is also the argument that the model fits:

| Today | Where | Becomes |
|---|---|---|
| a card (`__pmm__.toml` with fqn + impl) | 88 in `inspect/src_embed.inc.h` | `CARD` node under `NAMESPACE` chain |
| a registry module row | `registry/__impl__.rs:273`, `MOD_MAX = 128` | the `CARD` node itself |
| a registry export row (name, kind, ptr, sig) | `registry/__impl__.rs:233` | `EXPORT` node under the card |
| a `pymergetic.types` descriptor | `types/__types__.h:109` | `TYPE` node; identity by id, not pointer |
| a type field (name, hash, offset, type) | `types/__types__.h:86` | `FIELD` node under the type |
| a knob (module, leaf, soft, hard, used) | `util/limits/__types__.h:33` | `LIMIT` node under its card |
| an fs file (path, bytes, monotonic id) | `metal/fs/__impl__.c:9` | `BLOB` node; the id it already assigns becomes the node id |
| an embedded card source file | `src_embed.inc.h`, 25 MB | `BLOB` node under the card (see `05`) |
| a build record (fqn, sources, symbol names) | `build/__types__.h:559` | `CODE` nodes under `FN` nodes |
| a boot lifecycle row + its dep edges | `pm_mod_boot` / `pm_mod_bootdep` sections | edges between `CARD` nodes |
| a live arena object | nothing names one today | `DATA` node under `ARENA` |

Two of those are worth pausing on. The fs card already assigns a stable monotonic id
per file and already returns it from `add`/`write` — and already preserves it across a
same-length rewrite (`fs/__impl__.c:106`, `:135-156`) — but there is no face that
resolves an id back to a file, so the id is write-only. And the boot dependency edges
are already a link-time-materialized graph in five bracketed sections
(`port/boards/X86_64_BIOS/link.ld:30-52`), which is the closest thing the tree has to
"records the linker laid out that must be updated together."

## Capacity

Node table depth, name table size, section count, journal depth and view buffer size
are knobs under `pymergetic.state`, following the rule already in force: a default is
a starting point, growth is on demand, and exhaustion is a refusal that names the knob
so the message cannot drift from the thing that moved. Concretely, that means the node
table grows through `pm_util_limits_grow` the way the edit card's node array does
(`metal/edit/__impl__.c:46`), and a refusal reads
`pymergetic.state.node` rather than a bare "out of memory".
