/*
 * T23 — guest-side pthread_create() / pthread_join() via wasm32-wasip1-threads.
 * Host already has wasi-threads + thread-manager; this proves a mod built
 * by scripts/build mod none can actually spawn a wasm thread and share a
 * write back through linear memory.
 */
#include <pthread.h>
#include <stdio.h>

static void *worker(void *arg)
{
	int *out = arg;

	*out = 42;
	return NULL;
}

int main(void)
{
	int value = 0;
	pthread_t tid;

	if (pthread_create(&tid, NULL, worker, &value) != 0) {
		printf("t23_pthread: pthread_create failed\n");
		return 1;
	}
	if (pthread_join(tid, NULL) != 0) {
		printf("t23_pthread: pthread_join failed\n");
		return 1;
	}
	if (value != 42) {
		printf("t23_pthread: worker did not write shared value (got %d)\n", value);
		return 1;
	}

	printf("t23_pthread: worker wrote %d\n", value);
	return 0;
}
