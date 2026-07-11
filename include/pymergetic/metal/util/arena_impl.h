/*
 * pm_metal_util_arena_* bodies. #include this from exactly one loader .c
 * per binary — never from more than one TU (see size_impl.h for the same
 * pattern and why).
 *
 * Layout: the arena control struct and every block header live inside the
 * caller's own buffer — no separate bookkeeping allocation. Blocks form a
 * doubly linked list in address order (next/prev = physically adjacent
 * blocks), so free() can coalesce in O(1) without a full-arena scan.
 */
#ifndef PYMERGETIC_METAL_UTIL_ARENA_IMPL_H_
#define PYMERGETIC_METAL_UTIL_ARENA_IMPL_H_

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

#endif /* PYMERGETIC_METAL_UTIL_ARENA_IMPL_H_ */
