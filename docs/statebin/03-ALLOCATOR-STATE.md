# The materialized allocator state

The claim: after the initial state is established, a first allocation runs *without a
scan or a reconstruction* of the occupancy and free structures. That is what separates
this from a snapshot that has to be thawed.

## What has to be in the file

For each arena: its span and alignment, the occupied intervals with the node id of the
object in each, the reserved intervals, the free intervals, and enough allocator
bookkeeping that the next `alloc` is a normal fast-path operation.

```c
typedef struct pm_state_arena_rec {   /* payload of an ARENA node */
    uint64_t span_len;
    uint32_t align;
    uint32_t backing_section;   /* the COPY section holding the initial bytes */
    uint64_t want_base;         /* 0 = position independent, else the base it needs */
    uint32_t n_occupied;
    uint32_t n_reserved;
    uint32_t n_free;
    /* three interval arrays follow, each { uint64_t off; uint64_t len; uint32_t node; } */
} pm_state_arena_rec_t;
```

Every `DATA` node under the arena points into it with an arena-relative offset, so the
directory and the intervals agree by construction and the boot check can prove it
(below).

## The obstacle, stated exactly

The tree's allocator is an arena over vendored TLSF 3.1
(`extmod/wasmmod/third_party/tlsf/`), and the good news is that the *entire* allocator
state already lives inside the caller's span: `pm_util_mem_arena_create` places the
arena header at the front of the caller's own region and seeds the first TLSF pool at
the top of that same span (`extmod/wasmmod/src/pymergetic/util/mem/__impl__.c:84-130`).
So the bytes are physically capturable today — take the span, you have the heap.

The bad news is that every internal link is an absolute pointer:

- The arena header holds `base`, `end`, `map_brk`, `heap_brk` and the `tlsf` handle as
  raw addresses (`util/mem/__impl__.c:26-35`).
- `block_header_t` holds `prev_phys_block`, `next_free`, `prev_free` as pointers
  (`third_party/tlsf/tlsf.c:303`).
- `control_t` holds `blocks[FL_INDEX_COUNT][SL_INDEX_COUNT]` of pointers *plus* an
  embedded `block_null` that empty free lists point at — a self-referential absolute
  address (`third_party/tlsf/tlsf.c:346`).

A captured span is therefore byte-exact but position-dependent. There are three honest
ways out, and the choice is a real fork in the design.

### Option A — fixed base, no fixups

Record `want_base`, map the section there (`MAP_FIXED` on a hosted seat, the memmap
pick on a board), and every captured pointer is valid because nothing moved.

Cheapest by far, and it needs no allocator change at all. The cost is that the artifact
is bound to a virtual address, which is fine on firmware (the boards already choose
their base — 64 MiB on BIOS, `0x40010000` on ARMV7_QEMU, `0x8000` on RV1106) and
tolerable on a hosted seat, but it means two artifacts cannot both be resident at their
recorded bases if those bases collide. That matters for the canary domain in
`07`, where a candidate generation runs beside the parent.

### Option B — capture intervals, reseed the allocator

Do not image the TLSF control block. Image the interval lists, and at boot create a
fresh TLSF over the span and hand it the free intervals in order.

Position-independent, no allocator change, and the reseed is O(number of intervals)
rather than O(heap). But it is a reconstruction, which is the thing the claim
distinguishes itself from — defensible only if the interval list *is* the free
structure and the reseed is a bounded transfer, not a walk of live data. Worth keeping
as the portable fallback for a seat that cannot honour `want_base`.

### Option C — relative-pointer allocator

Make the arena and its allocator offset-based internally, so an image is position
independent with no fixups and no reseed.

This is the clean answer and the expensive one: it means a TLSF variant whose links are
`uint32_t` offsets from the pool base. It also touches the hottest code in the system
and would need its own prove ladder. Not a first stage — but note that the offset form
also shrinks a block header by half on 64-bit, which is not nothing for the boards.

**Recommendation:** A first, then B as the portable path for seats that refuse a fixed
base, and C only if canary co-residency or a shared artifact cache makes A untenable.
Whichever is chosen, the artifact records which one it used, so a reader never has to
guess.

## The imager already has its walker

To *write* an arena image you need the occupied/free map, and TLSF ships exactly that:

```c
void tlsf_walk_pool(pool_t pool, tlsf_walker walker, void* user);
/* void (*tlsf_walker)(void* ptr, size_t size, int used, void* user) */
```

It is at `third_party/tlsf/tlsf.h:80`, alongside `tlsf_get_pool`, `tlsf_block_size` and
`tlsf_check_pool` — and **nothing in the tree calls any of them.** The symbols are
linked into the firmware images and unreferenced.

So the first piece of real work is small and self-contained: expose a walk face on
`pymergetic.util.mem` (`arena_walk` with a callback, plus `arena_check` over
`tlsf_check_pool`), which the card's face conspicuously lacks — its public surface is
scalar stats only (`arena_bytes`, `map_used`, `heap_used`, `spare`, `hole`, `overhead`,
per `util/mem/__exports__.h:16-43`). That face is useful on its own merits, it is
provable on every seat with the existing card tests, and it is the input to the imager.

One caveat the walk must respect: `pm_util_mem_arena_add_pool` explicitly admits pools
*outside* `[base, end)` and tracks them in `spare` (`util/mem/__impl__.c:284`). An
arena with spare pools is not a single span, so either the image records every pool as
its own interval set, or the imager refuses an arena that has grown outside its span
and says so.

## What boot must check before the first allocation

- Every interval lies inside the arena span.
- Occupied, reserved and free intervals **cover the span exactly and do not overlap**.
- Every `DATA` node's `loc` falls inside an occupied interval whose `node` field names
  that node.
- Every mandatory `DATA` node named by an entry record or a mandatory reference exists.
- Alignment: each occupied interval satisfies its node's `align`.
- If the arena image was captured under option A, the mapping landed at `want_base`.

A failure is the failure path from `02-CONTAINER.md`, never a partial hand-over. These
same checks are what the successor's boot checker runs before a candidate generation is
accepted (`07`), so they are one function with two callers — the same shape as the
existing build actor's rollback, where the failure path and the happy path share one
teardown.

## Alignment, and a wrinkle worth knowing

TLSF needs 16-byte (`max_align_t`) alignment, and the tree learned this the hard way:
the jit card's arena reallocator routes fresh allocations through
`pm_util_mem_memalign(arena, 16, size)` because misaligned blocks corrupt deep
expression parsing inside TCC (`src/pymergetic/metal/jit/c/__impl__.c:44-68`, which
records the observed `block()` SIGSEGV). An arena image must preserve alignment across
the copy, which in practice means the `COPY` section's own alignment is at least the
arena's, and the boot check verifies it rather than assuming it.

Separately: `pymergetic.types` records `instance_size` but **no alignment** on a type
descriptor (`types/__types__.h:109`), deriving field cell widths from field types
instead. A `TYPE` node record has to carry alignment explicitly, or an imaged object
graph cannot be validated against its own types.
