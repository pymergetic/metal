# The container and the boot path

The artifact is one file. A standard program loader must be able to load or map it and
reach the entry through a path that is entirely contained in the file — no external
translation step, no separate snapshot, no rebuild of the object graph.

## Header and section table

```c
typedef struct pm_state_header {      /* at file offset 0 */
    uint8_t  magic[4];        /* 'S''T''B''N' */
    uint32_t format_version;
    uint32_t target_class;    /* e.g. 0x8664, 0x28 (arm), 0x00 (wasm32) */
    uint32_t word_bits;
    uint64_t total_len;
    uint64_t state_id;        /* identity of this generation */
    uint64_t parent_state_id; /* 0 for a root generation */
    uint32_t section_off;     /* section table */
    uint32_t section_count;
    uint32_t entry_off;       /* entry record table */
    uint32_t entry_count;
    uint32_t root_node;       /* node id of the root, normally 1 */
    uint32_t header_digest;
} pm_state_header_t;

typedef struct pm_state_section {
    uint32_t id;
    uint32_t kind;        /* nodes, names, types, semantics, code, arena, journal, blob */
    uint32_t map;         /* PM_STATE_MAP_* below */
    uint32_t perms;       /* r / w / x */
    uint64_t file_off;
    uint64_t file_len;
    uint64_t mem_len;     /* >= file_len; the excess is reserved, zero-filled */
    uint32_t align;
    uint32_t target;      /* 0 = any; else the target this variant is for */
    uint32_t digest;
} pm_state_section_t;
```

Both tables sit in the metadata region ahead of the first section, and both are
themselves `SECTION` nodes in the tree, so the model has no privileged outside.

## Mapping kinds

Boot does exactly one of these per section and nothing else. There is no scan for
plausible pointers anywhere:

| `map` | Meaning |
|---|---|
| `PM_STATE_MAP_DIRECT` | map the file bytes unchanged, read-only |
| `PM_STATE_MAP_COPY` | copy the file bytes unchanged into a named arena, writable |
| `PM_STATE_MAP_RELOCATE` | copy, then apply *only* the individually listed fixups |
| `PM_STATE_MAP_BIND` | a logical resource slot bound at boot (console, clock, block device) |
| `PM_STATE_MAP_SELECT` | choose the contained code variant matching the checked target |

`PM_STATE_MAP_RELOCATE` carries a table where every entry names section, offset, width
and kind. That is a hard rule, not a default: a fixup that is not listed does not
happen, and a section that is not marked `RELOCATE` is never written to. The reason is
in `04-REFERENCES.md` — because references are node ids rather than addresses, the
fixup list stays tiny (a handful of entries, not one per pointer), and "tiny and
enumerated" is what makes the boot check in `07` able to prove every fixup admissible.

## Entry record

```c
typedef struct pm_state_entry {
    uint32_t target;          /* which target class this entry serves */
    uint32_t entry_fn;        /* node id of an FN */
    uint32_t entry_code;      /* node id of the CODE child selected for this target */
    uint64_t entry_off;       /* offset within that code body */
    uint32_t arena;           /* node id of the ARENA that must exist first */
    uint32_t reloc_table;
    uint32_t resource_table;
    uint32_t failure_fn;      /* node id of the FN to run when a check fails */
} pm_state_entry_t;
```

`failure_fn` is not decoration. A check that fails must run a contained failure path,
never hand control to a half-built state. This is the same posture as the boot
sequence in the tree today, which unwinds cards in reverse and destroys the arena
rather than continuing (`src/pymergetic/metal/boot/__impl__.c:174`).

## Boot sequence

1. Check magic, format version, target class, total length, and that both tables lie
   inside the file without overlapping the first section.
2. Reserve the arena spans named by the entry record.
3. Map every `DIRECT` section; copy every `COPY` section.
4. Apply *only* the listed fixups for `RELOCATE` sections.
5. Select the code variant for the checked target.
6. Bind the logical resources; a missing mandatory resource takes the failure path.
7. Check the node table, the layout records, the mandatory references, and the arena
   coverage (`03-ALLOCATOR-STATE.md`).
8. Register the internal path driver over the tree (`06-PATHS-AND-VIEWS.md`).
9. Resolve the entry node through the same ids and layout records everything else
   uses, and hand over.

Step 7 before step 9 is the whole point: the first allocation after boot uses an
already-materialized free structure, and the entry is resolved through the same
identity base that a path lookup uses. One base, four consumers — boot, references,
the external mount, the internal view.

## Where this meets the seats

The four boards differ in how their loader arrives, and the container has to sit
comfortably in all of them. What the tree already provides:

**The image knows its own extent.** `__pm_metal_image_base` and
`__pm_metal_image_end` are page-aligned and provided by the linker script on all
three ELF boards (`port/boards/X86_64_BIOS/link.ld:8`, `:66`; same in
`ARMV7_QEMU/link.ld`, `ARMV7_RV1106/link.ld`), read as weak externs by the boot card
(`metal/boot/__impl__.c:83`). UEFI has no linker script and reads `SizeOfImage` out of
the loaded PE header instead (`port/boards/X86_64_BIOS/main.c:51`). So "the artifact
can find itself" is solved on every board, by two different mechanisms.

**A magic-branded, loader-writable header at a fixed offset already exists.**
`.bootinfo` is a `'METL'`-tagged record at the very start of the BIOS image
(`port/boards/X86_64_BIOS/crt0.S:5`), and the 32-bit trampoline finds it by fixed
address, validates the magic, patches two fields, and takes the entry point from it
(`port/boards/X86_64_BIOS/trampoline_load.c:151`). `link32.ld:34` even carries
`ASSERT`s that fail the link if the trampoline and the embedded image would overlap.
That is precisely the shape of `pm_state_header_t`, at one third the size, already
working on a board.

**Appending a section without destroying identity is solved in-tree.** The pack
format's ELF writer appends a section and leaves a 28-byte `'WPSE'` trailer recording
the pre-append length, section-header offset, count and shstrtab index, so a strip
reproduces the original bytes exactly and a signature digest still verifies
(`extmod/wasmmod/src/pymergetic/wasmmod/pack/format/elf/section.c:201`, restore at
`:285`, append at `:301`). A state binary that wants to carry an added view or
attestation section without invalidating its own digest should use that trick rather
than invent one.

**The precedent for a section whose contents are a data structure is everywhere.**
Five linker-materialized record arrays live in `.data`, each bracketed by
`__start_*` / `__stop_*` — `pm_metal_externals`, `pm_mod_boot`, `pm_mod_bootdep`,
`pm_metal_drv`, `pm_metal_class` (`port/boards/X86_64_BIOS/link.ld:30-52`) — and three
generated blobs live in `.rodata`, the largest being the 25 MB embedded source table
that the build card reads as its authoritative unit list
(`inspect/src_embed.inc.h`, 88 cards, consumed by `build/__impl__.c:2189`).

## What is missing, precisely

- **No container.** Nothing in the tree writes a bootable artifact. The pack format is
  read-and-strip for wasm and AOT and append-only for ELF64 LE, and its write paths
  are `MICROPY_WASM_MALLOC`-based, so they are not usable from a freestanding seat as
  they stand.
- **No durable write of any kind.** A block write works on firmware
  (`drivers/blk/virtio/__impl__.c:236` does real descriptor-chain DMA), but the only
  three callers of `pm_metal_drivers_blk_write` in the tree are the cards' own unit
  tests. FAT is read-only — `grep write fs/__fat__.c` returns nothing. The fs card is a
  monotonic RAM arena whose own comment says a changed file leaks its predecessor's
  bytes (`fs/__impl__.c:136`). The one `fopen(..., "wb")` in the tree is a unix-only
  debug mirror, off by default, never read back (`workspace/__impl__.c:83`).
- **No chainload.** Nothing in the tree can hand control to a newly written image;
  there is no kexec, no UEFI `LoadImage` call, only `pm_metal_process_reboot`.

Those three, in that order, are stages 4, 6 and 7 of `09-PLAN.md`.
