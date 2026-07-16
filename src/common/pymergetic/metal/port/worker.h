/*
 * Port contract — one background-worker primitive: spawn a function on a
 * new OS thread, poll it non-blockingly, or join it blockingly. Generalizes
 * what src/linux/main.c used to do ad hoc with raw pthread_create()/
 * pthread_join()/pthread_tryjoin_np() for each background worker thread —
 * now a named port primitive, same spirit as port/lock.h, so
 * runtime/process.c's spawn() (one worker thread per process) stays
 * impl: common instead of leaking pthread_t into common code.
 */
#ifndef PYMERGETIC_METAL_PORT_WORKER_H_
#define PYMERGETIC_METAL_PORT_WORKER_H_

/* Opaque, fixed-size storage for one native thread handle (pthread_t on
 * linux, k_tid_t + stack on zephyr) — sized generously so every target's
 * native handle fits without this header ever #include-ing an OS header;
 * impl: bind .c files reinterpret the whole struct as their real type
 * (each has a _Static_assert guarding the size fits). Same pattern as
 * pm_metal_port_mutex_t in port/lock.h. */
typedef struct pm_metal_port_worker {
	union {
		void *pm_metal_port_worker_ptr_align;
		long long pm_metal_port_worker_int_align;
		unsigned char pm_metal_port_worker_storage[64];
	} pm_metal_port_worker_opaque;
} pm_metal_port_worker_t;

typedef int (*pm_metal_port_worker_fn)(void *arg);

/* impl: bind — src/linux/pymergetic/metal/port/worker.c
 *              src/zephyr/pymergetic/metal/port/worker.c
 *
 * spawn(): starts fn(arg) on a new thread, filling in *w. Caller owns arg's
 * lifetime — same convention as pthread_create() itself, spawn() does not
 * copy it. Returns 0/-1; on -1, *w is left untouched (caller must not
 * try_join()/join() it).
 *
 * try_join(): non-blocking. Returns 0 if the thread has already finished
 * and was just joined by this call (safe to spawn() over *w again after);
 * nonzero if it's still running (safe to call again later, *w unchanged).
 *
 * join(): blocks until the thread finishes, then joins it. Always safe to
 * call at most once per spawn() (calling it again, or after a try_join()
 * that already returned 0, is undefined — same as pthread_join() itself). */
int pm_metal_port_worker_spawn(pm_metal_port_worker_t *w, pm_metal_port_worker_fn fn, void *arg);
int pm_metal_port_worker_try_join(pm_metal_port_worker_t *w);
void pm_metal_port_worker_join(pm_metal_port_worker_t *w);

#endif /* PYMERGETIC_METAL_PORT_WORKER_H_ */
