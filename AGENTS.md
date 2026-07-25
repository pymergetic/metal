# Agent notes for this repo

## Prime directive: reading outranks writing

Before you write a single line, find out what's already there. This sits
above every other rule in this file — a change that is dialect-clean and
well-formatted but duplicates an existing helper, ops-struct slot, or
utility is a **worse** change than one that reuses something imperfect.

- Search intensively (grep/glob the real tree, not just what you remember
  or what's open in the editor) for an existing function, macro, header, or
  pattern that already does this or something close to it — check the
  obvious module first, then adjacent ones (`util/`, `port/`, the shared
  ops-struct modules), before assuming there's a gap.
- Prefer calling, extending, or lightly generalizing something that already
  exists over adding a new symbol next to it "to keep it simple" — a
  slightly less convenient call site beats a second copy of the same logic.
- But reuse is not free of cost either: before generalizing a shared
  function, find and check every existing caller — a change that quietly
  breaks or subtly reshapes another call site is not a win. And don't bolt
  extra params/branches onto a hot-path helper (a tight CAS loop, a
  per-frame/per-packet function, ...) just to fold in one more caller if
  that measurably adds CPU work there — a small, clearly-named sibling next
  to it beats a slower one-size-fits-all.
- Only add something new once you've actually confirmed nothing reusable
  exists — briefly say what you checked, don't add silently on a hunch.
- This applies to helpers, small utilities, constants, and "just one more"
  wrapper functions most of all — that's exactly the code that quietly
  double-exists in a large tree if nobody looks first.

Read [`docs/SOURCETREE.md`](docs/SOURCETREE.md) before touching anything under
`src/` or `include/` — it defines the tree layout and the C dialect rule.

## The other rule you must not relitigate

`src/pymergetic/metal/**` is uniform ISO C / `stdint.h` — **one spelling per
type, everywhere**: `uint32_t` not `UINT32`, `void` not `VOID`, `static` not
`STATIC`, `bool` not `BOOLEAN`. EDK2 headers/types (`Uefi.h`, `Library/*.h`,
`UINT32`, ...) are only allowed physically under `src/efi/**` / `src/bios/**`
— never by filename convention (no `*_port.c` exception), never "just this
once", never behind a compat `#define`.

This is strict because the project owner is autistic and a type that can
appear under two different spellings (even when typedef-identical) costs
real, avoidable reading effort for their pattern matching. Treat this as a
hard requirement, not a style preference to debate. See `.cursor/rules/metal-c-dialect.mdc`
and `docs/SOURCETREE.md` § "C dialect" for the full rule, the containment
pattern for genuine EDK2 primitives (`*_port.c` split), and the verification
`grep`.

Before finishing any change touching `src/pymergetic/metal/**`, run:

```
grep -rlE '#include\s*<(Uefi\.h|Library/|Protocol/|IndustryStandard/)' src/pymergetic/metal --include=*.c --include=*.h
```

It must return nothing.
