# Agent notes for this repo

Read [`docs/SOURCETREE.md`](docs/SOURCETREE.md) before touching anything under
`src/` or `include/` — it defines the tree layout and the C dialect rule.

## The one rule you must not relitigate

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
