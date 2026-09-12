# Paths, two access directions, and views

The claim has a specific and demanding shape: the *same* normalized path keys, node
ids, selectors and layout records must serve both an external mount of the **un-started**
file and an internal self-view inside the **running** instance. Differences between host
and target filesystem protocols do not change that logical identity.

## Paths are derived, not stored

A path is the chain of names from the root. `pm_state_by_path` walks components with
`child_named`; there is no second table mapping strings to ids.

That is a deliberate simplification of the claim's path-assignment table: having one is
satisfied by deriving one, and deriving it removes the failure mode the shared identity
base exists to prevent — a path table and a tree that disagree. What *is* stored is the
small set of things a pure tree walk cannot express:

```c
typedef struct pm_state_path_rec {   /* payload of a PATH node: an alias or a view mount */
    uint32_t target;        /* node id this path resolves to */
    uint32_t selector;      /* which part of it: whole, field, body, metadata */
    uint32_t view;          /* node id of a VIEW generator, 0 = stored bytes */
    uint16_t read_policy;   /* STORED_BYTES or GENERATED_VIEW */
    uint16_t write_policy;  /* NONE, RAW_OBJECT, or SEMANTIC_PATCH */
    uint16_t base_policy;   /* how a write must bind the base generation */
} pm_state_path_rec_t;
```

So aliases and view mounts are nodes too, and the default namespace is the tree itself.

## The namespace

```
/                       the root node
/sections/<name>        a region of the file
/types/<type>           a type record
/types/<type>/<field>   a field record
/modules/<dotted>/...   the card tree; a card's children are its knobs, fns, exports
/objects/<id>           a live object by id
/objects/<id>/<field>   a field of it, through its type's layout
/code/<fn-id>/<target>  one code body
/arenas/<name>          span, occupied, reserved, free
/tools/<name>           a contained capability
/views/source/<gen>/<node>     a generated source view
/views/semantic/<node>         a generated semantic view
/journal/<seq>          an activation record
```

Reading `/objects/0x2001/value` resolves the object id, finds its layout record, applies
its type's field offset, and reads the live bytes — the same three records boot used to
place the object in the first place.

## The same path, two directions

**Outside**, on a host, the file is mounted without starting its program. The reader
parses the header, the section table, the node table and the name table, and answers
reads by locating bytes in the file or by running a view generator over the records it
finds there.

**Inside**, after boot, the same tree is already mapped and the internal driver answers
the same path keys against the same nodes — no re-parse, no second index. Both
directions normalize a path with one rule and resolve it through one set of records.

The one asymmetry that is legitimate: outside, a `DATA` node in an arena reads its
*initial* bytes from the `COPY` section; inside, it reads the live bytes. Same node,
same layout record, different space — which is exactly what `pm_state_loc_t`'s `space`
field is for.

## What the tree has

Four disjoint path namespaces exist today, and none of them is this:

| Card | Namespace | Backing | Enumerable |
|---|---|---|---|
| `pymergetic.metal.fs` | arbitrary `/...`, plus `/esp/...` from FAT | arena list + read-only FAT | no |
| `pymergetic.metal.workspace` | `/src/pymergetic/...`, `/src/externals/...` | writes into fs | count only |
| `pymergetic.metal.inspect` | `/src/<fqn>[/<file>]`, `/docs`, `/build`, ... | the 25 MB embedded table | manifest JSON |
| `pymergetic.metal.net.http.asgi` | the HTTP route table | per-route handler | no |

**The fs card is a flat association list, not a filesystem.** `struct file` is a
singly-linked list of `{next, name, data, len, id, used}` and lookup is a linear walk
with a whole-string compare — `/` has no meaning to the card
(`metal/fs/__impl__.c:9`, `:52`). Its `__types__.h` declares nothing. The FAT
fall-through is a compile-time `if/else` in `stat` and `read`, not a backend vtable
(`fs/__impl__.c:158`), so **there is no pluggable path backend anywhere in the kernel**.
There is no readdir on either tier: directory entries exist in the FAT cache but both
`fat_stat` and `fat_read` refuse them (`fs/__fat__.c:438`, `:450`).

And µPy's own VFS is not wired to any of it — the only `MICROPY_VFS` reference in the
metal tree is the negative one that stubs `mp_import_stat`
(`port/upy/firmware_upy.c:191`), with the capability surface reporting
`vfs_static: false` (`port/upy/modmetal.c:911`).

**The inspect card is the closest working thing to path → object → view.** Its route
grammar is `/src/<fqn>` for a manifest and `/src/<fqn>/<file>` for a body
(`inspect/__impl__.c:1384`), resolved by binary search over the fqn-sorted embedded
table and then a linear scan of that card's files (`:907`). And `/docs/<fqn>/<fn>` is a
genuinely *generated* view: it locates the export macro in the embedded source text,
walks backwards over the contiguous comment block, and renders prose plus params as
JSON, reporting the file and line it came from (`inspect/__impl__.c:1322`, `:948`). A
generated view that carries provenance back into the source already exists.

Its limits: read-only (no POST on `/src/*`), no object ids anywhere (a card is a string,
a file is a string), no listing above the card level, and the `/src/` prefix with an
empty fqn 404s.

**The pack format's source view is the best precedent for the two directions.**
`mp_wasm_source_open_file(path)` opens an artifact by host path and gives a
path-addressed view of its contents *without instantiating anything* — the external
mount — while `mp_wasm_source_open_name(pack_name)` resolves the same content through
the loaded pack registry from inside (`extmod/wasmmod/src/pymergetic/wasmmod/pack/source.h:69-81`).
And unlike the fs card, that namespace is enumerable and hierarchical:
`mp_wasm_source_read`, `mp_wasm_source_mount_prefix`, `mp_wasm_source_list_files`,
`mp_wasm_source_list_modules`, `mp_wasm_source_list_submodules` — the last one being
readdir over immediate children, and `mount_prefix` handling the dotted-module to
slash-path duality (`source.h:86-105`). Two constructors, four iterators, one identity.
That is the shape to copy.

## What has to be built

1. **A backend interface in the kernel.** The fs card needs a real ops table so the
   state tree can be one backend among several, instead of a hardcoded fall-through.
   `pymergetic.util.gen`'s `GenSink` trait is the right shape (`read` returning
   `Ok(None)` for absent, `write`, three implementations) but it is host-side Rust; the
   kernel needs the same contract in C, and the tree already has the C mirror
   convention for it — `pm_util_gen_vfs_ops_t` with the size-probe idiom where a NULL
   buffer means "tell me the length" (`util/gen/__types__.h:11`).
2. **Enumeration.** `child_count` / `child_at` on the node tree, and a readdir on the
   path face that uses them. This is what nothing has today.
3. **A view generator registry.** `VIEW` nodes with generator ids, so a read can return
   stored bytes or a generated projection under one namespace — today generated things
   (`/docs`, `ast_dump`, rsx lowering) and stored things (`/src`) live in different
   routes with different shapes.
4. **A writable view.** The only mutating route in the whole inspect card is
   `POST /build[/<fqn>]`, which rebuilds and does not accept source bytes.
5. **µPy integration**, so `import` and `os` can see the tree. Today they cannot see
   anything.

## The UI already tells the story

The HTTP surface that exists — source panes at `/src/*`, build records at `/build/*`,
object bytes at `/build/object/*` with windowed reads because the largest card object is
5.6 MB against a 1 MiB body window, live events at `/build/events` — is a browser-shaped
view of exactly the objects this model wants to name. When paths derive from the tree,
those routes stop being hand-registered special cases and become one wildcard over the
node namespace.
