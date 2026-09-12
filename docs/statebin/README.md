# The state binary

One file that boots directly, carries its whole program state as an object graph, can
be read and written through a path namespace from outside and from inside, and can
produce a checked successor generation of itself.

The filing this documents is `EINREICHUNG-GESAMT-V4-SIGNED` / `STATE-BINARY-2026`,
"Unmittelbar startbares Zustands-Binärartefakt mit identitätsgebundener
Code-Dateisystem-Schnittstelle" (10 claims). The lifecycle *around* the artifact —
measure, derive a need, bind a change contract, build a variant in staging, canary it,
let a separate gate accept it, activate transactionally — is the companion filing
`METAL-PLASMID-2026` and is out of scope here except where the two meet
(`07-SUCCESSOR-AND-ACTIVATION.md`).

## Reading order

| Doc | Question it answers |
|---|---|
| `01-OBJECT-MODEL.md` | What is an object, what is its id, how does the root drill down |
| `02-CONTAINER.md` | What the file looks like and what boot does with it |
| `03-ALLOCATOR-STATE.md` | How a live heap becomes bytes and comes back without a scan |
| `04-REFERENCES.md` | How one object names another, and why that removes `dlsym` |
| `05-SEMANTIC-AUTHORITY.md` | What replaces text source as the meaning of the program |
| `06-PATHS-AND-VIEWS.md` | The path namespace, generated views, and writing one back |
| `07-SUCCESSOR-AND-ACTIVATION.md` | Delta, dependency closure, boot check, journal, generation swap |
| `08-INVENTORY.md` | Evidence: what the tree has today, with file and line, and what it lacks |
| `09-PLAN.md` | Stages, each with a prove on every seat |

`08-INVENTORY.md` is the one to read first if you want to know how far away this is.
The short version: the *tools* mostly exist (an in-image C compiler, an in-image
Rust-to-C compiler, an ELF relocator, a runtime type catalog, an embedded source
tree, generation-checked handles), and the *state* almost entirely does not (no
object directory, no arena image, no journal, no durable write path at all).

## The one idea

Everything is an object, and there is exactly one way to name one: a node id.

A node has a kind, a parent, ordered children, a name, a type, a location, and a
generation. That is the whole model. A type is a node. A function is a node. A code
body compiled for one target is a node under that function. A live counter in a heap
is a node. A capacity knob is a node under the card that owns it. A section of the
file is a node. The node table itself is a node.

Everything else in the design is a consequence:

- A **path** is the chain of names from the root, so the path namespace is not a
  second table to keep in sync — it is a view of the tree. (The patent permits a
  path map; deriving it is one way of having one.)
- A **reference** is a node id plus a sub-offset, never an address, so an image is
  position-independent by construction and "linking" is resolving ids against layout
  records rather than looking up symbol names in a host process.
- A **generated view** is a projection of a subtree, and writing it back means
  parsing it and mapping the result onto the ids it came from — surviving nodes keep
  their ids, new nodes get fresh ones, deletions are recorded as ids that went away.
- A **successor generation** is: a delta over node ids, the dependency closure of
  those ids, every affected record rewritten together, then a boot check, then
  acceptance. Unchanged regions are taken byte for byte.

## Three non-negotiables

**Nothing may die where it should refuse.** The artifact contains its own compiler.
Today that compiler's out-of-room paths are fatal: upstream TCC's default reallocator
prints and calls `exit(1)` (`externals/tcc/libtcc.c:258`), and on seats where the
arena window is installed our reallocator honestly returns NULL, which
`tcc_mallocz` immediately `memset`s. A self-rebuilding artifact whose build step can
kill the running generation has no loop. TCC's own `error_set_jmp_enabled` /
`longjmp` channel (`libtcc.c:697`, armed for the whole of `tcc_compile` at `:814`)
is the way to convert exhaustion into a typed refusal.

**No capacity is a static reservation.** Every limit is a knob under the card that
owns it — soft, hard, default, live-used — movable at runtime from C, C++, Rust and
Python (`src/pymergetic/util/limits/`, mirrored in wasmmod). The node table, the
section table, the journal and the view buffers all follow that rule. A default is
where a state starts, not where it stops.

**Every seat, in the same change.** Host C, unix µPy, emcc browser, and all four
firmware boards. A stage that only works on the host seat is not done; see
`09-PLAN.md`, where each stage carries its own prove per seat. This matters more here
than usual, because the single largest structural gap in the tree is exactly a
host-only capability: in-kernel linking needs `dlopen`, `mmap(MAP_32BIT)` and
`/proc/self/maps`, so the four boards refuse it outright
(`src/pymergetic/metal/build/__impl__.c:1599`, "link: no loader on this seat").
The id-based reference model in `04-REFERENCES.md` is what closes that gap.

## Provisional naming

Card names are proposals, not decisions. The split follows where the work has to run:

| Proposed card | Impl | Lives in | Owns |
|---|---|---|---|
| `pymergetic.state` | c | wasmmod | node table, ids, kinds, tree walk, name table, layout records |
| `pymergetic.state.image` | c | wasmmod | container header/section table, reader, checker, writer |
| `pymergetic.state.view` | c | wasmmod | view generation and identity-preserving write-back |
| `pymergetic.metal.state.boot` | c | metal | the seat fill: materialize an image, bind resources, hand over |
| `pymergetic.metal.state.store` | c | metal | durable write: block device, journal, generation swap |

The first three sit in wasmmod so the metal-less seats (`packages/micropython-wasmmod`)
get them too; only the last two need a board. Names use the ABI convention already in
force: `init` pairs with `deinit`, `create` with `destroy`, and `fini` is not a word
(`.cursor/rules/c-abi-names.mdc`).

## Status

Design only. No code has been written for any of this. Nothing in
`packages/metalpython` implements a node table, an arena image, a journal, or a
successor formatter today.
