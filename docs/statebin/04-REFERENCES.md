# References, and why they remove `dlsym`

A reference is a node id and a sub-offset. Never an address, never a symbol name
resolved at load time against whatever the host happens to have.

```c
typedef struct pm_state_ref {
    uint32_t from;        /* node id of the referring object */
    uint32_t from_slot;   /* which slot within it (byte offset or ordinal) */
    uint32_t to;          /* node id of the referent */
    uint64_t to_off;      /* sub-offset within the referent */
    uint16_t kind;        /* CALL, DATA, ENTRY, TYPE, IMPORT */
    uint16_t binding;     /* MANDATORY or OPTIONAL */
} pm_state_ref_t;
```

A running instance derives the address from the referent's layout record plus the
mapping of the section it lives in. Nothing in the file says `0x7f...`.

## Why this is the load-bearing decision

Today the in-kernel link resolves names against the live process. The resolver calls
`dlopen(NULL, RTLD_LAZY)` and `dlsym` for every external
(`src/pymergetic/metal/build/__impl__.c:1389`), decides code-versus-data by consulting
a cached snapshot of `/proc/self/maps` (`:1325`, `ctx->exec_ranges[512]`), and — because
the relocated image is mapped `MAP_32BIT` while process symbols sit high — synthesizes
a `movabs %rax; jmp *%rax` thunk from a pre-allocated RWX table for every far code
target (`:1434`, `PM_BUILD_THUNK_SLOTS 512`, `PM_BUILD_THUNK_BYTES 16`).

That is three POSIX facilities no board has. The consequence is not subtle:
`PM_METAL_BUILD_HAS_ELF` is set in exactly three places — the host `Makefile:176`, the
unix µPy `metal.mk:121`, and `tools/ksweep.c:147` — and on all four boards
`pm_metal_build_link` falls through to a refusal, `"link: no loader on this seat"`
(`build/__impl__.c:1599`). Firmware seats can compile (TCC is linked in via
`port/fw_tcc.mk`) but cannot link, cannot publish, and therefore cannot rebuild
themselves. **This is the single biggest gap between "in-kernel rebuild works" and "the
boards can rebuild themselves,"** and it is not a config flag.

Id-based references dissolve it. If a call site refers to node 0x1003 rather than to
the string `pm_ip_socket_open`, then resolving it needs the node table and the section
mapping and nothing else — no dynamic loader, no process symbol table, no `/proc`. The
only names that survive are the ones that genuinely come from outside the artifact, and
those are enumerated in an imports section, checked at boot, and refused loudly when
absent.

## The imports section, and how big it actually is

An `IMPORT` node names a symbol the seat must provide, with the signature it is
expected to have and whether it is mandatory. I measured the real surface on the
`X86_64_BIOS` board build, across the 79 card objects in
`port/build/X86_64_BIOS-mp-repl/cards`:

| Undefined symbols needed by card objects | Count |
|---|---|
| distinct symbols in total | **512** |
| `pm_*` (the tree's own) | **332** |
| `mbedtls_*` | 70 |
| libc, TCC, board glue | 110 |

And of those 332 `pm_*` symbols, cross-referenced against the 652 names registered
through `PM_MOD_EXPORT_C` in both source trees:

| | Count |
|---|---|
| registry-exported | **211** |
| **not exported** | **121** |

The 121 include whole clusters like `pm_ip_arena`, `pm_ip_arp`, `pm_ip_arp_ask`,
`pm_ip_arp_cap`, `pm_ip_arp_input`, `pm_ip_arp_lookup`, `pm_ip_arp_queue`,
`pm_ip_arp_tick`, `pm_ip_arp_used`, `pm_ip_csum`, `pm_ip_eth_tx`,
`pm_ip_if_pending_be` — internal seams between a card's own translation units, which
the registry never needed to know about because the linker resolved them.

Per-card undefined counts, for scale: console 10, edit 18, net/ip 44, build 47.

Two conclusions follow. First, **a linker that knew only the registry would fail on
about a third of the tree** — so the node model must carry intra-card references
(`FN` to `FN` within a card) as ordinary node-id edges, not as registry lookups.
Second, the genuinely external surface is 180 symbols (70 mbedtls plus 110
libc/TCC/board), which is small enough to enumerate as `IMPORT` nodes and check at
boot. That is a manageable number, and it is the honest boundary of "self-contained."

For comparison, the in-kernel sweep currently produces **1153 exported symbols across
86 linked card images** (`build/ksweep_report.txt`), so the export side is already an
order of magnitude larger than the import side. The artifact is mostly inward-facing,
which is exactly what makes the id model cheap.

## Bounded relocation

Because references are ids, the fixup table stays tiny — the handful of places where a
real machine address must be written into a mapped section: the arena base, the address
of the internal path driver, a program-counter-relative call into a selected code
variant. Each entry names section, offset, width and kind, and the boot check proves
each one admissible before it is applied.

The rule is the important part: **a field that is not named in the table is not
written, and a section not marked `RELOCATE` is not touched.** No section is searched
for plausible pointers. This is what makes the difference between a checkable artifact
and a heuristic one, and it is the same discipline the ELF relocator already follows
internally — it applies `RELA` entries, it does not guess (see the relocation reader at
`extmod/wasmmod/src/pymergetic/wasmmod/pack/format/elf/load.c:728`).

## Precedents to build on

- **`pm_addr_t`** — `{ uint32_t space; uint64_t off; }` with spaces NATIVE, SHARED and
  MODULE, plus `pm_buf_t` as `{ptr, len}` (`registry/__types__.h:107`). Today
  `SPACE_NATIVE` just carries a host VA, so it is a discriminated union rather than
  true relocation, but the vocabulary is in place and documented.
- **The ELF relocator** is the in-tree precedent for a relocation walker that applies
  an enumerated table over a mapped image, including TLS materialization
  (`pack/format/elf/load.c:1554`).
- **`mp_wasm_elf_image_load_multi`** already binds a name to the first definition
  across N objects in dependency order, which is the operation an id-based linker
  replaces with a table lookup.

## What this does not solve

Calling *out* to an import still needs the seat's address, and on a board a far call
may still need a thunk. The difference is that the thunk count is bounded by the number
of `IMPORT` nodes rather than by the number of call sites, and the addresses come from
a resource binding table that boot fills, not from `dlsym`. Also unresolved: the ELF
adapter path publishes a *partial* export set when its fixed 32-slot thunk pool is
exhausted, without recording a refusal (`ports/micropython/packbind.c:472` and the
`elf_export_cb` path) — a state binary must refuse the whole activation instead, the
way the wasm loader already rolls back a full load when its 8 adapter slots run out
(`wasmmod/loader/__impl__.rs:1614`).
