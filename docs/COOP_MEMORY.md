# Cooperative memory & CPU layout

Freestanding Metal (EFI first). Dual-span arena + per-job coroutines.
See [EFI.md](EFI.md).

---

## Dual-span arena

One claimed conventional hole (`EfiLoaderData`):

```text
low (map_brk →)                    (← heap_brk) high
[ pages / stacks / job grants ][ HOLE ][ TLSF pools ]
```

| Side | API | Grows |
|------|-----|--------|
| Low | `metal_map` / `METAL_MEM_MAP` | upward |
| High | TLSF via `metal_alloc` (HEAP) | downward (new pools) |
| Middle | free hole | shrinks until OOM |

- **Looper stacks** and **job regions** come from the map side (private grants).
- **General malloc** is one locked TLSF on the heap side (grows as needed).
- No fixed LOCAL½ / SHARED¼ split. “Shared” is coop + messages, not a slab class.

---

## Front end

```c
void *metal_alloc(size_t n, metal_mem_flags_t where, metal_id_t id);
void *metal_map(size_t n);
void *metal_lookup(metal_id_t id);
```

| `where` | Backend |
|---------|---------|
| `HEAP` / `LOCAL` / `SHARED` | TLSF (high side) |
| `MAP` | page map (low side) |

`id != 0` publishes into a circular list (nodes on the heap).

---

## Per-CPU runloop

```text
CPU k: LOCAL stack (mapped) + inbox (heap, METAL_ID_INBOX(k))
         metal_run_enter → SwitchStack → drain until STOP
```

Messages: `PING` / `ADD` / `CORO` / `STOP`.  
`CORO` cookie = `metal_coro_t *` → `metal_coro_resume`.

---

## Per-job coroutines

Looper stays **outside**. A job is `fn + state + in + out` inside a **private map grant**.

```c
metal_coro_t *metal_coro_create(fn, state_n, in_n, out_n, region_n);
void         *metal_coro_alloc(coro, n);   /* bump inside grant */
int           metal_coro_spawn(coro, cpu); /* post CORO to inbox */
```

- Alloc inside the job cannot clash with other jobs.
- Any coro (or bring-up) may `spawn` another (asyncio `create_task` shape).
- Sync C calls OK if short; async = spawn / later await.

---

## Rules

1. Claim RAM once before ExitBootServices.
2. Maps vs heap: `metal_free` is TLSF only; maps use `metal_unmap` (LIFO).
3. Prefer job-private grants for parallel work; heap for small shared control.
4. Yield = return to runloop (stackless steps).
