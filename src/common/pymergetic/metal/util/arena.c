/*
 * pm_metal_util_arena_* — impl: common (see util/arena.h; wasm32 mods reach
 * this same code via this file's own wasi-style import registration at
 * the bottom, not via a second compiled copy of this file).
 *
 * Control block lives in a host-only slot table (never in guest linear
 * memory). The caller's buf holds only block headers + payloads. Block
 * links are offsets from the arena base (not raw host pointers). free()
 * requires a live magic + a walk from head so double-free / bogus
 * pointers are rejected.
 */
#include "pymergetic/metal/util/arena.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>

#include "pymergetic/metal/util/fourcc.h"

#define PM_METAL_UTIL_ARENA_ALIGN (sizeof(void *))
#define PM_METAL_UTIL_ARENA_MAGIC_USED PM_METAL_UTIL_FOURCC('A', 'R', 'N', 'U')
#define PM_METAL_UTIL_ARENA_MAGIC_FREE PM_METAL_UTIL_FOURCC('A', 'R', 'N', 'F')
#define PM_METAL_UTIL_ARENA_OFF_NULL ((size_t)SIZE_MAX)
#define PM_METAL_UTIL_ARENA_MAX_SLOTS 32

typedef struct pm_metal_util_arena_block {
	size_t size; /* payload capacity, excludes this header */
	int used;
	uint32_t magic;
	size_t next_off; /* SIZE_MAX = end; else byte offset from arena base */
	size_t prev_off;
} pm_metal_util_arena_block_t;

struct pm_metal_util_arena {
	uint8_t *base;
	size_t buf_len;
	size_t head_off;
	size_t used;
};

typedef struct {
	int used;
	uint32_t gen;
	pm_metal_util_arena_t arena;
} pm_metal_util_arena_slot_t;

static pm_metal_util_arena_slot_t g_pm_metal_util_arena_slots[PM_METAL_UTIL_ARENA_MAX_SLOTS];

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

static pm_metal_util_arena_slot_t *pm_metal_util_arena_slot_claim(void)
{
	int i;

	for (i = 0; i < PM_METAL_UTIL_ARENA_MAX_SLOTS; i++) {
		pm_metal_util_arena_slot_t *s = &g_pm_metal_util_arena_slots[i];

		if (!s->used) {
			s->used = 1;
			s->gen++;
			if (s->gen == 0) {
				s->gen = 1;
			}
			memset(&s->arena, 0, sizeof(s->arena));
			return s;
		}
	}
	return NULL;
}

static pm_metal_util_arena_slot_t *pm_metal_util_arena_slot_of(const pm_metal_util_arena_t *arena)
{
	int i;

	if (!arena) {
		return NULL;
	}
	for (i = 0; i < PM_METAL_UTIL_ARENA_MAX_SLOTS; i++) {
		pm_metal_util_arena_slot_t *s = &g_pm_metal_util_arena_slots[i];

		if (s->used && &s->arena == arena) {
			return s;
		}
	}
	return NULL;
}

static uint32_t pm_metal_util_arena_handle_of(const pm_metal_util_arena_t *arena)
{
	pm_metal_util_arena_slot_t *s = pm_metal_util_arena_slot_of(arena);
	int idx;

	if (!s) {
		return 0;
	}
	idx = (int)(s - g_pm_metal_util_arena_slots);
	return (s->gen << 16) | (uint32_t)(idx + 1);
}

static pm_metal_util_arena_t *pm_metal_util_arena_from_handle(uint32_t handle)
{
	uint32_t idx;
	uint32_t gen;
	pm_metal_util_arena_slot_t *s;

	if (handle == 0) {
		return NULL;
	}
	idx = (handle & 0xffffu) - 1u;
	gen = handle >> 16;
	if (idx >= PM_METAL_UTIL_ARENA_MAX_SLOTS) {
		return NULL;
	}
	s = &g_pm_metal_util_arena_slots[idx];
	if (!s->used || s->gen != gen) {
		return NULL;
	}
	return &s->arena;
}

pm_metal_util_arena_t *pm_metal_util_arena_init(void *buf, size_t buf_len)
{
	size_t block_hdr = pm_metal_util_arena_block_hdr();
	pm_metal_util_arena_slot_t *slot;
	pm_metal_util_arena_t *arena;
	pm_metal_util_arena_block_t *first;

	if (!buf || buf_len < block_hdr) {
		return NULL;
	}

	slot = pm_metal_util_arena_slot_claim();
	if (!slot) {
		return NULL;
	}
	arena = &slot->arena;
	first = (pm_metal_util_arena_block_t *)buf;

	first->size = buf_len - block_hdr;
	first->used = 0;
	first->magic = PM_METAL_UTIL_ARENA_MAGIC_FREE;
	first->next_off = PM_METAL_UTIL_ARENA_OFF_NULL;
	first->prev_off = PM_METAL_UTIL_ARENA_OFF_NULL;

	arena->base = (uint8_t *)buf;
	arena->buf_len = buf_len;
	arena->head_off = 0;
	arena->used = 0;

	return arena;
}

void *pm_metal_util_arena_alloc(pm_metal_util_arena_t *arena, size_t size)
{
	size_t need;
	size_t block_hdr;
	size_t off;
	pm_metal_util_arena_block_t *b;

	if (!arena || size == 0 || !pm_metal_util_arena_slot_of(arena)) {
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

	if (!arena || !ptr || !pm_metal_util_arena_slot_of(arena)) {
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
		return;
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
	if (!arena || !pm_metal_util_arena_slot_of(arena)) {
		return 0;
	}
	return arena->used;
}

/*
 * wasi-style import bridge — arena handles are opaque host ids (never
 * guest linear-memory offsets). Pool buffers / alloc results still use
 * automatic app<->native translation for '*'+'~' / returned app offsets.
 */
#include "wasm_export.h"

static int32_t pm_metal_util_arena_init_native(wasm_exec_env_t exec_env, void *buf, uint32_t buf_len)
{
	pm_metal_util_arena_t *arena;

	(void)exec_env;
	arena = pm_metal_util_arena_init(buf, (size_t)buf_len);
	if (!arena) {
		return 0;
	}
	return (int32_t)pm_metal_util_arena_handle_of(arena);
}

static int32_t pm_metal_util_arena_alloc_native(wasm_exec_env_t exec_env, uint32_t handle,
						uint32_t size)
{
	pm_metal_util_arena_t *arena = pm_metal_util_arena_from_handle(handle);
	void *ptr;

	if (!arena) {
		return 0;
	}
	ptr = pm_metal_util_arena_alloc(arena, (size_t)size);
	if (!ptr) {
		return 0;
	}
	return (int32_t)wasm_runtime_addr_native_to_app(wasm_runtime_get_module_inst(exec_env), ptr);
}

static void pm_metal_util_arena_free_native(wasm_exec_env_t exec_env, uint32_t handle, void *ptr)
{
	pm_metal_util_arena_t *arena = pm_metal_util_arena_from_handle(handle);

	(void)exec_env;
	pm_metal_util_arena_free(arena, ptr);
}

static int32_t pm_metal_util_arena_used_native(wasm_exec_env_t exec_env, uint32_t handle)
{
	(void)exec_env;
	return (int32_t)pm_metal_util_arena_used(pm_metal_util_arena_from_handle(handle));
}

static NativeSymbol g_pm_metal_util_arena_native_symbols[] = {
	{"pm_metal_util_arena_init", (void *)pm_metal_util_arena_init_native, "(*~)i", NULL},
	{"pm_metal_util_arena_alloc", (void *)pm_metal_util_arena_alloc_native, "(ii)i", NULL},
	{"pm_metal_util_arena_free", (void *)pm_metal_util_arena_free_native, "(i*)", NULL},
	{"pm_metal_util_arena_used", (void *)pm_metal_util_arena_used_native, "(i)i", NULL},
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
