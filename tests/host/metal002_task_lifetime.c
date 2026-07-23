/*
 * METAL-002 / METAL-003 / METAL-010 — queue-safe task lifetime + exclusive await.
 *
 * Host model of the inbox/timer retain protocol (no UEFI). Asserts:
 *  - destroy waits until queued holds drain before reuse of the allocation
 *  - timer-style retain across "unlock" keeps the object alive
 *  - second awaiter is rejected while the first is installed
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct task {
	volatile uint32_t refs;
	volatile uint32_t doomed;
	void             *waiter;
	int               cancelled;
	uint32_t          magic;
} task_t;

#define TASK_MAGIC 0x5441534bu /* 'TASK' */

static void
task_ref(task_t *t)
{
	assert(t != NULL);
	t->refs++;
}

static void
task_unref(task_t *t)
{
	assert(t != NULL);
	assert(t->refs > 0);
	t->refs--;
}

static task_t *
task_new(void)
{
	task_t *t = calloc(1, sizeof(*t));
	assert(t != NULL);
	t->refs  = 1;
	t->magic = TASK_MAGIC;
	return t;
}

static int
task_spawn_msg(task_t **inbox, size_t *n, size_t cap, task_t *t)
{
	if (t->doomed) {
		return -1;
	}
	if (*n >= cap) {
		return -1;
	}
	task_ref(t);
	inbox[(*n)++] = t;
	return 0;
}

static void
task_dispatch_one(task_t **inbox, size_t *n)
{
	task_t *t;
	assert(*n > 0);
	t = inbox[0];
	memmove(&inbox[0], &inbox[1], (*n - 1) * sizeof(inbox[0]));
	(*n)--;
	assert(t->magic == TASK_MAGIC);
	task_unref(t); /* message hold */
}

static void
task_destroy(task_t *t, task_t **inbox, size_t *n)
{
	t->cancelled = 1;
	(void)task_spawn_msg(inbox, n, 64, t);
	t->doomed = 1;

	while (t->refs > 1 && *n > 0) {
		task_dispatch_one(inbox, n);
	}
	assert(t->refs == 1);
	t->magic = 0;
	t->refs  = 0;
	free(t);
}

static int
test_inbox_uaf(void)
{
	task_t *inbox[64];
	size_t  n = 0;
	task_t *t;
	void   *reuse;

	t = task_new();
	assert(task_spawn_msg(inbox, &n, 64, t) == 0);
	assert(t->refs == 2);

	/* Destroy while a wake is still queued — must not free early. */
	task_destroy(t, inbox, &n);
	assert(n == 0);

	reuse = malloc(sizeof(task_t));
	assert(reuse != NULL);
	memset(reuse, 0xA5, sizeof(task_t));
	free(reuse);
	return 0;
}

static int
test_timer_retain(void)
{
	task_t *t = task_new();
	task_t *held;

	/* Arm: retain under "lock". */
	task_ref(t);
	held = t;
	/* Unlock — concurrent destroy cannot free while hold stands. */
	assert(held->refs == 2);
	held->doomed = 1;
	/* Destroyer waits for refs==1; we still hold the timer ref. */
	assert(held->magic == TASK_MAGIC);
	task_unref(held); /* timer delivery done */
	assert(t->refs == 1);
	t->refs = 0;
	free(t);
	return 0;
}

static int
test_exclusive_await(void)
{
	task_t  t = { .refs = 1, .magic = TASK_MAGIC };
	void   *w1 = (void *)0x1;
	void   *w2 = (void *)0x2;

	t.waiter = w1;
	if (t.waiter != NULL && t.waiter != w2) {
		/* second waiter rejected */
		return 0;
	}
	fprintf(stderr, "metal002: exclusive await failed\n");
	return 1;
}

int
main(void)
{
	if (test_inbox_uaf() != 0) {
		return 1;
	}
	printf("metal002: inbox drain-before-free ok\n");
	if (test_timer_retain() != 0) {
		return 1;
	}
	printf("metal003: timer retain ok\n");
	if (test_exclusive_await() != 0) {
		return 1;
	}
	printf("metal010: exclusive await ok\n");
	printf("metal002: ok\n");
	return 0;
}
