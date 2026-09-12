# The face and the channels

One semantic chokepoint, many transports. This doc fixes the rank of the VFS face: it is not
*a* consumer of the core — it is **the only door**. The programmatic API is a client of it,
exactly like the kernel is. Boot's record checks are clients of it. There is no operation that
    5|reaches records by a second path.

```
   FUSE adapter      syscall layer      python bridge     CLI      boot resolver
   (host kernel)     (on a board)       (sugar lib)      (sugar)  (also a client)
        |                 |                 |                |          |
        +--------- all speak path operations -------------+---------+
                                 |
                     face  (normalize * resolve * readdir * read * stage * commit)
                                 |
                     core  (node table, names, layout, refs, types)
                                 |
                     artifact bytes (mmap / mapped / copied spans)
```

This collapses claim (e)'s two directions — external mount (62) and internal driver (63) — into
**one face with two transports**. Hosted, the host kernel mediates (FUSE speaks path ops to the
face); on a board, the artifact's own syscall layer mediates, speaking the *same* path ops to
the same face. "Runs as a real OS" versus "runs as a program" stops being a property of the
artifact and becomes a property of who implements the mediator.

Doc `01`'s face section is re-ranked by this doc: `by_path` and `bytes` are *the* interface;
`by_id`, `child_at`, `child_named` are core-internal ops that only the face calls. Nothing can
sidestep the namespace, because there is no other door to sidestep through.

## The ops

Provisional list. Every op takes a session and returns a typed refusal; a handle is
`{id, generation}` and a stale handle is refused, never redirected.

    35|
- `resolve(path) -> handle` — normalize once, then walk; no path table (`06`).
- `readdir(handle) -> ordered child names + kinds` — computed from the parent's directory
  payload, never a shadow index.
- `read(handle, off, len) -> bytes` — `STORED_BYTES`: locate via layout record; `GENERATED_VIEW`:
  run the view generator over the records; size probe via NULL buffer (`util/gen/__types__.h:11`
  idiom).
- `stage(handle, bytes)` — a write lands in a transaction node under `/state/transactions/<tx>`;
  there is no direct-mutation path anywhere.
- `commit(tx)` — parse, map to ids, validate, closure, delta, format, check, accept
  (`07`). One routine no matter which channel the write arrived through.
- `verify(artifact) -> report` — the checker as a callable; also the face op that boot's checks
  and the acceptance gate share.
- `meta(handle) -> {kind, type, gen, size, digest}` — identity facts for every object; the
  xattr-shaped information every channel needs.

Two properties carry over from `01` unchanged: `resolve` walks the tree (no path table to
disagree with it), and every accessor takes a handle so every accessor can refuse.

## The discipline that keeps the face honest

1. **Pure function of records.** The face holds handles and the staging area, nothing else.
   Caching is generation-keyed memoization — `(node, generation) -> bytes` — never an
   independent index. A shadow listing cache is the second representation this design exists to
   prevent.
2. **Handles make it fast.** One resolution per binding, not per access; exactly what kernel
   `open()` is, so the programmatic sugar and the syscall layer are structurally identical.
3. **Everything is path-shaped.** Tools, transactions, views are namespace objects (the claim's
   path classes `/state/tools/...`, `/state/views/...`, `/state/transactions/<id>/...`). Running
   the checker is writing to its tool endpoint; committing is invoking the commit tool over a
   transaction node. If a capability cannot be expressed as a path-addressable object, it is
   probably a hidden second representation.
4. **Writes are staged, then committed.** POSIX gives byte chunks with no commit point, so a
   channel-agnostic write accumulates in a transaction node and `commit(tx)` is the semantic
   endpoint. Read-only channels bind no `stage`/`commit` at all.

Boot is a client too: step 9 of `02`'s sequence — resolve the entry through the same ids —
happens *through the face*, after boot's checks have themselves read candidate records through
it. The only residue outside the namespace is the loader handoff itself: the first instructions
the platform runs before the face exists. That boundary is written down here so no later change
adds a second privileged path into boot.

## Channels

A channel is a binding of the face to a transport — the same fill pattern as `io.fetch`
(POSIX / Metal park / `js.fetch`): the face is the module, channels are the fills. A channel may
**decode, but never decide**: percent-decoding a URL or FTP's `CWD` (which is just handle
acquisition) is transport syntax; resolution, normalization and write policy remain face-only.

| Channel | Role | Phase A (IDE) | Phase B (self-host) |
|---|---|---|---|
| `direct` | in-process client lib | the `stbn` CLI/REPL — primary authoring/inspection surface | face impl ported inward, C |
| `vfs` | kernel mediation | pyfuse3 mount — `grep`/`diff` is dev workflow, not deployment | external mount driver (stays host-side per claim (62)) |
| `http` | network mediation, trust boundary | FastAPI inspector (browser dev-UI, agent surface) | same binding; the gate's read path for acceptance |
| `ftp` | the weird-channel stress test | pyftpdlib | the conformance toy it always was |

Each channel carries a **capability map** recorded on its channel node: which face ops this
binding carries. `ftp:` binds read-only; `http:` carries PUT-to-stage / POST-to-commit; `vfs:`
carries stage. A verb a channel does not bind is a typed refusal naming the channel — the same
discipline as a knob refusing by name.

The channel registry is records in the namespace — `/state/channels/<name>` — so an external
reader learns from the artifact itself what surfaces it serves, and the conformance corpus
enumerates channels from the artifact rather than a hand-list. Channels split into **contained**
ones (compiled into the artifact via `SELECT_TARGET_CODE`: the board syscall layer, the internal
self-view) and **external** ones (host processes — the mount driver and inspector).

## The conformance corpus

The corpus is what makes "same routine everywhere" mechanical instead of aspirational: the
same path list, driven through every bound channel plus the direct binding, outputs
byte-compared. Stdlib clients cover all of Phase A's channels from one script — `open()` through
the mount, `urllib` for HTTP, `ftplib` for FTP, the CLI for `direct`. The corpus owns three
diffs: same-path byte equality across channels; the schema dump (the canonical layout table's
computed offsets against the generated header's `_Static_assert`s and against actual emitted
bytes) between the Python axioms and every other spelling; and cross-platform, cross-hash-seed
seed determinism (`sha256` of a fresh build on Linux, macOS, Windows, and under two
`PYTHONHASHSEED`s — must match).

The server is a dispatcher, not an interpreter — no long-lived process sits between a client
and the face with semantics of its own. Where a channel needs a host process (FUSE loop, HTTP
inspector), it is a dumb transport loop: decode, face call, translate refusal, respond.

## What is not in v1

- Event/notification reads (generation-change broadcasts) — poll the generation node; a
  blocking events path is a Plan 9 nicety for later.
- FTP over TLS, virtual hosts, any HTTP caching — the channels are bindings, not services.
- Any normalization variant. One rule, in the core, evaluated at one place.