/*
 * pm_metal_util_arena_* — impl: common (see util/arena.h; wasm32 mods reach
 * this same code via this file's own wasi-style import registration at
 * the bottom, not via a second compiled copy of this file).
 *
 * Layout: the arena control struct and every block header live inside the
 * caller's own buffer — no separate bookkeeping allocation. Blocks form a
 * doubly linked list in address order (next/prev = physically adjacent
 * blocks), so free() can coalesce in O(1) without a full-arena scan.
 */
#include "pymergetic/metal/util/arena.h"

#include <stdint.h>

#define PM_METAL_UTIL_ARENA_ALIGN (sizeof(void *))

typedef struct pm_metal_util_arena_block {
	size_t size; /* payload capacity, excludes this header */
	int used;
	struct pm_metal_util_arena_block *next; /* next block by address, NULL at end */
	struct pm_metal_util_arena_block *prev; /* prev block by address, NULL at start */
} pm_metal_util_arena_block_t;

struct pm_metal_util_arena {
	pm_metal_util_arena_block_t *head;
	size_t used;
};

static size_t pm_metal_util_arena_round_up(size_t n)
{
	return (n + (PM_METAL_UTIL_ARENA_ALIGN - 1)) & ~(PM_METAL_UTIL_ARENA_ALIGN - 1);
}

pm_metal_util_arena_t *pm_metal_util_arena_init(void *buf, size_t buf_len)
{
	size_t arena_hdr = pm_metal_util_arena_round_up(sizeof(pm_metal_util_arena_t));
	size_t block_hdr = pm_metal_util_arena_round_up(sizeof(pm_metal_util_arena_block_t));

	if (!buf || buf_len < arena_hdr + block_hdr) {
		return NULL;
	}

	pm_metal_util_arena_t *arena = (pm_metal_util_arena_t *)buf;
	pm_metal_util_arena_block_t *first =
		(pm_metal_util_arena_block_t *)((uint8_t *)buf + arena_hdr);

	first->size = buf_len - arena_hdr - block_hdr;
	first->used = 0;
	first->next = NULL;
	first->prev = NULL;

	arena->head = first;
	arena->used = 0;

	return arena;
}

void *pm_metal_util_arena_alloc(pm_metal_util_arena_t *arena, size_t size)
{
	if (!arena || size == 0) {
		return NULL;
	}

	size_t need = pm_metal_util_arena_round_up(size);
	size_t block_hdr = pm_metal_util_arena_round_up(sizeof(pm_metal_util_arena_block_t));
	pm_metal_util_arena_block_t *b;

	for (b = arena->head; b; b = b->next) {
		if (!b->used && b->size >= need) {
			break;
		}
	}
	if (!b) {
		return NULL;
	}

	/* Split off the remainder as its own free block if there's enough
	 * room left for another header plus at least one aligned unit. */
	if (b->size >= need + block_hdr + PM_METAL_UTIL_ARENA_ALIGN) {
		pm_metal_util_arena_block_t *rest =
			(pm_metal_util_arena_block_t *)((uint8_t *)b + block_hdr + need);

		rest->size = b->size - need - block_hdr;
		rest->used = 0;
		rest->next = b->next;
		rest->prev = b;
		if (rest->next) {
			rest->next->prev = rest;
		}

		b->next = rest;
		b->size = need;
	}

	b->used = 1;
	arena->used += b->size;

	return (uint8_t *)b + block_hdr;
}

void pm_metal_util_arena_free(pm_metal_util_arena_t *arena, void *ptr)
{
	if (!arena || !ptr) {
		return;
	}

	size_t block_hdr = pm_metal_util_arena_round_up(sizeof(pm_metal_util_arena_block_t));
	pm_metal_util_arena_block_t *b =
		(pm_metal_util_arena_block_t *)((uint8_t *)ptr - block_hdr);

	arena->used -= b->size;
	b->used = 0;

	if (b->next && !b->next->used) {
		pm_metal_util_arena_block_t *n = b->next;

		b->size += block_hdr + n->size;
		b->next = n->next;
		if (b->next) {
			b->next->prev = b;
		}
	}

	if (b->prev && !b->prev->used) {
		pm_metal_util_arena_block_t *p = b->prev;

		p->size += block_hdr + b->size;
		p->next = b->next;
		if (p->next) {
			p->next->prev = p;
		}
	}
}

size_t pm_metal_util_arena_used(const pm_metal_util_arena_t *arena)
{
	return arena ? arena->used : 0;
}

/*
 * wasi-style import bridge — see size.c's own bridge comment for the
 * general signature-string rules this follows.
 *
 * init()'s return and alloc()'s return are app-offset-shaped (guest
 * linear-memory addresses) but WAMR natives cannot declare a pointer
 * *result* type (only i32/i64/f32/f64) — so both wrappers return a plain
 * 'i' plus an explicit wasm_runtime_addr_native_to_app() call to convert
 * the other way, since only *parameters* get automatic app->native
 * translation.
 *
 * alloc()/free()/used() take the caller's `arena`/`ptr` as a bare '*'
 * (checked length 1, not the full struct size — there is no natural
 * "length" argument at those call sites to pair a '~' with). A guest
 * holds an opaque handle it never dereferences itself, so this is the
 * same trade-off any opaque-handle-over-'*' native API makes; documented
 * here rather than hidden.
 */
#include "wasm_export.h"

static int32_t pm_metal_util_arena_init_native(wasm_exec_env_t exec_env, void *buf, uint32_t buf_len)
{
	pm_metal_util_arena_t *arena = pm_metal_util_arena_init(buf, (size_t)buf_len);

	if (!arena) {
		return 0; /* 0 is never a valid app offset for a live object — NULL */
	}
	return (int32_t)wasm_runtime_addr_native_to_app(wasm_runtime_get_module_inst(exec_env), arena);
}

static int32_t pm_metal_util_arena_alloc_native(wasm_exec_env_t exec_env, void *arena, uint32_t size)
{
	void *ptr = pm_metal_util_arena_alloc((pm_metal_util_arena_t *)arena, (size_t)size);

	if (!ptr) {
		return 0;
	}
	return (int32_t)wasm_runtime_addr_native_to_app(wasm_runtime_get_module_inst(exec_env), ptr);
}

static void pm_metal_util_arena_free_native(wasm_exec_env_t exec_env, void *arena, void *ptr)
{
	(void)exec_env;
	pm_metal_util_arena_free((pm_metal_util_arena_t *)arena, ptr);
}

static int32_t pm_metal_util_arena_used_native(wasm_exec_env_t exec_env, void *arena)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_arena_used((const pm_metal_util_arena_t *)arena);
}

static NativeSymbol g_pm_metal_util_arena_native_symbols[] = {
	{"pm_metal_util_arena_init", (void *)pm_metal_util_arena_init_native, "(*~)i", NULL},
	{"pm_metal_util_arena_alloc", (void *)pm_metal_util_arena_alloc_native, "(*i)i", NULL},
	{"pm_metal_util_arena_free", (void *)pm_metal_util_arena_free_native, "(**)", NULL},
	{"pm_metal_util_arena_used", (void *)pm_metal_util_arena_used_native, "(*)i", NULL},
};

int pm_metal_util_arena_native_register(void)
{
	if (!wasm_runtime_register_natives(PM_METAL_UTIL_ARENA_WASI_MODULE, g_pm_metal_util_arena_native_symbols,
					    sizeof(g_pm_metal_util_arena_native_symbols)
						    / sizeof(g_pm_metal_util_arena_native_symbols[0]))) {
		return -1;
	}
	return 0;
}
