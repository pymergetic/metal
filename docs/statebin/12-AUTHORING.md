# Authoring: models, templates, and the one format

How humans and generators write artifact content before the artifact can edit itself. The
front end is a compiler with gates, not a parser with hope — and the moment it can produce
everything the seed needs, the soma takes over and this layer becomes the germline's
notation (`11`).

## The pipeline

```
read (stdlib json, dup-key refused)
  -> expand ($def/$call/$include; no recursion, no expressions)
  -> validate (pydantic discriminated unions — the shape gate)
  -> check (the checker: refs, types, closure, entry — the meaning gate; the same
            function boot and acceptance call, never a second one)
  -> stage through the face -> commit (one routine everywhere, `10`)
```

Digests and fixed points are taken over the **expanded**, canonical form. Authoring cargo
(`$comment`, `defs`) never reaches the models and never reaches the bytes.

## One format: JSON

One ink, one loader, one canonicalizer — the same rule as the tree's C dialect (one
spelling per thing, everywhere). YAML was declined for the largest hazard surface plus a
dependency (no stdlib parser, implicit typing, aliases that copy while pretending to
reference); XML for no map syntax, no sane canonical form, and cardinality leaking into
syntax shape; TOML stays the notation of the *card tree's* genotype (`__pmm__.toml`) — a
different germline, untouched.

Five rules define the format:

1. **One loader**: stdlib `json`, duplicate keys refused via `object_pairs_hook` — the
   silent last-win collapse becomes a typed refusal before any model sees a dict.
2. **One canonicalizer**: `sort_keys=True`, compact separators, LF, UTF-8, integers only
   (models reject floats). This one-liner is the digest input for every fixed point,
   cross-platform compare and corpus diff.
3. **`$comment` everywhere** — cargo, not syntax. Deterministically dropped at expand, or
   routed to a doc node. Comments are not authority ("historische Kommentare ... muessen
   nicht erhalten sein").
4. **Arrays are order-as-presentation, objects are sets, execution order is edges.**
   Emit order comes from the schema, never from input key order; sets are canonicalized
   by `sort_keys`. Execution order never lives in a container at all (below).
5. **Files are pretty, digests are compact.** `rxf fmt` renders for humans; the canonical
   form is machine-only. Two renderings of one syntax, never two formats.

## Order, precisely

Execution order is **edges, never list position** — the same call every SSA IR makes, and
for this artifact it is load-bearing, not stylistic: if position carried execution, an
inserted op would shift the identity of every op after it, break the write-back map
("survivors keep their ids"), and blow up every delta from one node to N. An op list is
storage; `then`/`else`/`pred`/`in` are the meaning. Reordering op lines in a file must
never change the compiled bytes — and does not, because canonical op order is a *derived*
pre-order walk from entry (tie-broken by name), computed by the emitter, recorded in the
format spec as defining-level so both implementations (Python oracle, contained C) agree.

Children are authored as **arrays** (listing order is canonical and meaning-carrying,
mirroring `01`); genuinely unordered things (imports, exports, phi preds) are **objects**
and get their order from `sort_keys`. Never author children as a name-keyed object — a
mapping cannot express listing order, so author intent would become unauthorable.

## Three kinds of reuse — not synonyms

| Mechanism | Layer | Expands to | Identity | Use for |
|---|---|---|---|---|
| `call` op | semantic (in-artifact, runtime) | nothing — it *is* the thing | one node, a real call-graph edge | runtime composition: the OS calling itself |
| `Ref` cell | semantic | nothing | one node, N pointers | aliasing, sharing, graphs |
| `$call` | authoring only | a **copy** of a template body | N nodes, N ids | not repeating yourself while writing |

YAML anchors were refused for silently conflating copy and reference; `$call` is honest
expansion — two `$call`s mint two independent nodes sharing authoring, never identity.
Want one-thing-many-pointers? That is `Ref`. Want runtime composition? That is `call`.

The composition/computation boundary: **JSON gets composition (`$def`/`$call`/`$include`),
Python gets computation.** No expressions, arithmetic, loops or logic in JSON — the moment
authoring needs any of those, it lives in the Python API, which produces the same models
through the same gates. This is the house's "source stays in its language" rule applied to
a data format: programs are `.py`, data is `.json`.

## The template system (authoring layer only)

- `defs` — named parameterized bodies: `{ "params": {...}, "body": {...} }`.
- `$call` — instantiate with `args`; `$param` tokens interpolate in strings; a token that
  is the entire string splices any JSON value. Each `$call` is an independent node.
- `$include` — splice another file (module trees may be split; the manifest is the tree
  shape, not a second source of truth).
- **Refused**: recursion (a def whose body `$call`s anything, directly or transitively),
  cycles, unknown params, expressions. All typed refusals naming the def and the path.
- Expansion is deterministic and eager; the checker and the emitter never see `$`-keys.

## The models (`rxf[author]`)

Pydantic v2, discriminated unions keyed on `kind` / `op` — mapping 1:1 onto node kinds.
`extra="forbid"` turns unknown keys into typed refusals with paths. Integers only.
Field `offset` is **always present** — one schema, one form, no profiles. JSON files are
machine-produced (the API, views, `rxf fmt`), and derivation lives only in the API
(below), so every file carries complete explicit offsets. The models are a
**generated spelling of `schema.py`** (banner-gated, like the C header and
the JSON Schema file); nobody hand-maintains a second schema, and the corpus checks all
spellings agree. Pydantic stays an extra: the core and the certification path remain
stdlib-only (`11`).

Shape gate (models) vs meaning gate (checker): pydantic proves the data is well-formed;
the checker proves the *program* is coherent — ref targets exist, operand types match op
signatures, offsets fit instance sizes, the call graph is closed, the entry is reachable,
imports are enumerated. Schema-valid is not "legit"; checked is legit. The gates are
separate because they catch disjoint failure classes, and the meaning gate is the same
function boot runs — authoring is just its third caller.

## The Python API — the real macro language

Templates are plain functions returning models; composition is function calls, loops,
comprehensions — the full language, deterministic by construction (same args, same
models). `A.build(tree, out=...)` runs the same pipeline as the JSON path: models ->
checker -> face -> commit. One model layer, two front doors, same bytes.

At micro-OS scale this API is where generated content lives (tables, opcode dispatch
records, per-target variants) — anything a program can derive better than a hand can type.

### The type registry

Intrinsic types are Python values, not strings: `A.u32`, `A.ptr(elem)`, `A.arr(u8, 64)`.
`@A.ty` turns an annotated class into a TYPE node — annotation order is field order is
offset order:

```python
@A.ty
class CounterState:
    value: A.u32 = A.MUT
    limit: A.u32 = A.MUT
```

The assignment slot carries flags — `A.MUT` (mutable), `A.val(n)` (const), `A.at(off)`
(pinned offset) — and `Fields.add` offers the imperative twin for generated content
(`tables.add(...)` in a loop). Both produce the same `FieldRec` list through the same
gates. Registry contents shadow-check against the artifact's intrinsics (ids 1..31,
`03`); a build type shadowing an intrinsic is a typed refusal.

### Layout is computed once, in the API — and recorded everywhere else

The API is the **only** place layout is ever computed: `@A.ty` and `Fields.add` pack
left-to-right with alignment, honor pins, and hand complete explicit offsets plus the
derived `instance_size`/`align` to the models. JSON records the result — offset
required, always present, one schema, no authoring tolerances. The checker verifies
(fit `instance_size`, no overlap) regardless of who wrote the number, and the canonical
form and TYPE cell carry explicit offsets too. Boot never re-computes layout — and
**the contained C core never implements a layout algorithm at all**: it verifies
recorded offsets and digests them. Like canonical JSON ordering, layout stays an
external-tool responsibility forever, and the porting ladder never carries it.

## What gets authored as ops — and what does not

Declarations (types, fields, signatures, imports, exports, knobs), small functions,
glue, and effect annotations are born for this notation. **Large bodies are not**: nobody
hand-writes a net stack or a compiler as an op list, in any ink. Bodies come through
`05`'s text views — text -> parse -> ops -> the same gates — which is why `05`'s stage
order (declarations as records long before bodies as records) is also the authoring
order. The op-level notation is the *target* the view pipeline emits into, not the
surface humans write compilers in.

## The two compile roles (unchanged from `11`)

1. **Seed compile** — full tree -> full binary; ids minted deterministically; the frozen
   builder; regeneration must be byte-identical (CI row, corpus).
2. **In-lineage edit** — a *view of a subtree*; parse, map onto existing ids, validate,
   closure, delta. Templates expand in both roles; the expanded subtree is matched
   against existing nodes by the change mapper.

Blurring them would recompile the world and re-mint ids on every edit; the delta/journal
story depends on keeping them apart.

## Worked sample

The counter example (`increment`, with an authored-out-of-order `const` proving
edges-not-position) plus a `ring` template instantiated twice lives in the README of the
`rxf` pill and doubles as a corpus fixture: the fixture must compile to the same bytes
from its JSON form, its Python-API form, and after `rxf fmt` reformatting.