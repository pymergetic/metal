/*
 * pm_metal_util_arena_* — impl: common (see util/arena.h; wasm32 mods reach
 * this same code via this file's own wasi-style import registration at
 * the bottom, not via a second compiled copy of this file).
 *
 * Layout: the arena control struct and every block header live inside the
 * caller's own buffer — no separate bookkeeping allocation. Block links are
 * *offsets from the arena base* (not raw host pointers), so a wasm guest that
 * can write its own linear memory cannot forge next/prev into host address
 * space. free() also requires a live magic + a walk from head so double-free
 * / bogus pointers are rejected.
 */
#include "pymergetic/metal/util/arena.h"

#include <limits.h>
#include <stdint.h>

#include "pymergetic/metal/util/fourcc.h"

#define PM_METAL_UTIL_ARENA_ALIGN (sizeof(void *))
#define PM_METAL_UTIL_ARENA_MAGIC_USED PM_METAL_UTIL_FOURCC('A', 'R', 'N', 'U')
#define PM_METAL_UTIL_ARENA_MAGIC_FREE PM_METAL_UTIL_FOURCC('A', 'R', 'N', 'F')
#define PM_METAL_UTIL_ARENA_OFF_NULL ((size_t)0)

typedef struct pm_metal_util_arena_block {
	size_t size; /* payload capacity, excludes this header */
	int used;
	uint32_t magic;
	size_t next_off; /* 0 = end; else byte offset from arena base */
	size_t prev_off;
} pm_metal_util_arena_block_t;

struct pm_metal_util_arena {
	uint8_t *base;
	size_t buf_len;
	size_t head_off;
	size_t used;
};

static size_t pm_metal_util_arena_round_up(size_t n)
{
	size_t aligned_max = SIZE_MAX - (PM_METAL_UTIL_ARENA_ALIGN - 1);

	if (n > aligned_max) {
		return 0; /* cannot round without overflow */
	}
	return (n + (PM_METAL_UTIL_ARENA_ALIGN - 1)) & ~(PM_METAL_UTIL_ARENA_ALIGN - 1);
}

static size_t pm_metal_util_arena_block_hdr(void)
{
	return pm_metal_util_arena_round_up(sizeof(pm_metal_util_arena_block_t));
}

static size_t pm_metal_util_arena_off_of(const pm_metal_util_arena_t *arena, const void *p)
{
	return (size_t)((const uint8_t *)p - arena->base);
}

static int pm_metal_util_arena_off_ok(const pm_metal_util_arena_t *arena, size_t off)
{
	size_t hdr = pm_metal_util_arena_block_hdr();

	if (off == PM_METAL_UTIL_ARENA_OFF_NULL) {
		return 0;
	}
	if (off < pm_metal_util_arena_round_up(sizeof(pm_metal_util_arena_t))) {
		return 0;
	}
	if (off + hdr > arena->buf_len) {
		return 0;
	}
	if ((off % PM_METAL_UTIL_ARENA_ALIGN) != 0) {
		return 0;
	}
	return 1;
}

static pm_metal_util_arena_block_t *pm_metal_util_arena_block_at(pm_metal_util_arena_t *arena,
								 size_t off)
{
	if (!pm_metal_util_arena_off_ok(arena, off)) {
		return NULL;
	}
	return (pm_metal_util_arena_block_t *)(arena->base + off);
}

pm_metal_util_arena_t *pm_metal_util_arena_init(void *buf, size_t buf_len)
{
	size_t arena_hdr = pm_metal_util_arena_round_up(sizeof(pm_metal_util_arena_t));
	size_t block_hdr = pm_metal_util_arena_block_hdr();
	pm_metal_util_arena_t *arena;
	pm_metal_util_arena_block_t *first;

	if (!buf || buf_len < arena_hdr + block_hdr) {
		return NULL;
	}

	arena = (pm_metal_util_arena_t *)buf;
	first = (pm_metal_util_arena_block_t *)((uint8_t *)buf + arena_hdr);

	first->size = buf_len - arena_hdr - block_hdr;
	first->used = 0;
	first->magic = PM_METAL_UTIL_ARENA_MAGIC_FREE;
	first->next_off = PM_METAL_UTIL_ARENA_OFF_NULL;
	first->prev_off = PM_METAL_UTIL_ARENA_OFF_NULL;

	arena->base = (uint8_t *)buf;
	arena->buf_len = buf_len;
	arena->head_off = arena_hdr;
	arena->used = 0;

	return arena;
}

void *pm_metal_util_arena_alloc(pm_metal_util_arena_t *arena, size_t size)
{
	size_t need;
	size_t block_hdr;
	size_t off;
	pm_metal_util_arena_block_t *b;

	if (!arena || size == 0) {
		return NULL;
	}

	need = pm_metal_util_arena_round_up(size);
	if (need == 0) {
		return NULL;
	}
	block_hdr = pm_metal_util_arena_block_hdr();
	b = NULL;

	for (off = arena->head_off; off != PM_METAL_UTIL_ARENA_OFF_NULL; ) {
		size_t next_off;
		pm_metal_util_arena_block_t *cur = pm_metal_util_arena_block_at(arena, off);

		if (!cur) {
			return NULL;
		}
		next_off = cur->next_off;
		if (!cur->used && cur->size >= need) {
			b = cur;
			break;
		}
		off = next_off;
	}
	if (!b) {
		return NULL;
	}

	/* Split off the remainder as its own free block if there's enough
	 * room left for another header plus at least one aligned unit. */
	if (b->size >= need + block_hdr + PM_METAL_UTIL_ARENA_ALIGN) {
		size_t rest_off = off + block_hdr + need;
		pm_metal_util_arena_block_t *rest =
			(pm_metal_util_arena_block_t *)(arena->base + rest_off);

		rest->size = b->size - need - block_hdr;
		rest->used = 0;
		rest->magic = PM_METAL_UTIL_ARENA_MAGIC_FREE;
		rest->next_off = b->next_off;
		rest->prev_off = off;
		if (rest->next_off != PM_METAL_UTIL_ARENA_OFF_NULL) {
			pm_metal_util_arena_block_t *n =
				pm_metal_util_arena_block_at(arena, rest->next_off);

			if (!n) {
				return NULL;
			}
			n->prev_off = rest_off;
		}

		b->next_off = rest_off;
		b->size = need;
	}

	b->used = 1;
	b->magic = PM_METAL_UTIL_ARENA_MAGIC_USED;
	arena->used += b->size;

	return (uint8_t *)b + block_hdr;
}

void pm_metal_util_arena_free(pm_metal_util_arena_t *arena, void *ptr)
{
	size_t block_hdr;
	size_t off;
	size_t walk;
	pm_metal_util_arena_block_t *b;
	int found = 0;

	if (!arena || !ptr) {
		return;
	}
	if ((uint8_t *)ptr < arena->base || (uint8_t *)ptr >= arena->base + arena->buf_len) {
		return;
	}

	block_hdr = pm_metal_util_arena_block_hdr();
	off = pm_metal_util_arena_off_of(arena, (uint8_t *)ptr - block_hdr);
	if (!pm_metal_util_arena_off_ok(arena, off)) {
		return;
	}

	/* Must be reachable from head — rejects forged interior pointers. */
	for (walk = arena->head_off; walk != PM_METAL_UTIL_ARENA_OFF_NULL; ) {
		pm_metal_util_arena_block_t *w = pm_metal_util_arena_block_at(arena, walk);

		if (!w) {
			return;
		}
		if (walk == off) {
			found = 1;
			break;
		}
		walk = w->next_off;
	}
	if (!found) {
		return;
	}

	b = pm_metal_util_arena_block_at(arena, off);
	if (!b || b->magic != PM_METAL_UTIL_ARENA_MAGIC_USED || !b->used) {
		return; /* double-free / corrupt */
	}

	if (arena->used < b->size) {
		return;
	}
	arena->used -= b->size;
	b->used = 0;
	b->magic = PM_METAL_UTIL_ARENA_MAGIC_FREE;

	if (b->next_off != PM_METAL_UTIL_ARENA_OFF_NULL) {
		pm_metal_util_arena_block_t *n = pm_metal_util_arena_block_at(arena, b->next_off);

		if (n && !n->used && n->magic == PM_METAL_UTIL_ARENA_MAGIC_FREE) {
			b->size += block_hdr + n->size;
			b->next_off = n->next_off;
			if (b->next_off != PM_METAL_UTIL_ARENA_OFF_NULL) {
				pm_metal_util_arena_block_t *nn =
					pm_metal_util_arena_block_at(arena, b->next_off);

				if (nn) {
					nn->prev_off = off;
				}
			}
			n->magic = 0;
		}
	}

	if (b->prev_off != PM_METAL_UTIL_ARENA_OFF_NULL) {
		pm_metal_util_arena_block_t *p = pm_metal_util_arena_block_at(arena, b->prev_off);

		if (p && !p->used && p->magic == PM_METAL_UTIL_ARENA_MAGIC_FREE) {
			p->size += block_hdr + b->size;
			p->next_off = b->next_off;
			if (p->next_off != PM_METAL_UTIL_ARENA_OFF_NULL) {
				pm_metal_util_arena_block_t *nn =
					pm_metal_util_arena_block_at(arena, p->next_off);

				if (nn) {
					nn->prev_off = b->prev_off;
				}
			}
			b->magic = 0;
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
