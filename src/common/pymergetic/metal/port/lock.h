/*
 * Port contract — a single mutex primitive, just enough for runtime.c and
 * memory/bytecode.c to guard their own shared state across threads. See
 * docs/RUNTIME.md "Concurrency" for the threading contract this backs.
 */
#ifndef PYMERGETIC_METAL_PORT_LOCK_H_
#define PYMERGETIC_METAL_PORT_LOCK_H_

/* Opaque, fixed-size storage for one native mutex (pthread_mutex_t on
 * linux, struct k_mutex on zephyr) — sized generously so every target's
 * native mutex fits without this header ever #include-ing an OS header;
 * impl: bind .c files reinterpret the whole struct as their real type
 * (each has a _Static_assert guarding the size fits). The union just
 * forces pointer/long-long alignment, nothing in it is ever read here. */
typedef struct pm_metal_port_mutex {
	union {
		void *pm_metal_port_mutex_ptr_align;
		long long pm_metal_port_mutex_int_align;
		unsigned char pm_metal_port_mutex_storage[64];
	} pm_metal_port_mutex_opaque;
} pm_metal_port_mutex_t;

/* One-time init gate (pthread_once / atomic). Zero-init or PM_METAL_PORT_ONCE_INIT. */
typedef struct pm_metal_port_once {
	union {
		void *pm_metal_port_once_ptr_align;
		long long pm_metal_port_once_int_align;
		unsigned char pm_metal_port_once_storage[64];
	} pm_metal_port_once_opaque;
} pm_metal_port_once_t;

#define PM_METAL_PORT_ONCE_INIT \
	{                       \
		{               \
			0       \
		}               \
	}

/* impl: bind — src/linux/pymergetic/metal/port/lock.c
 *              src/zephyr/pymergetic/metal/port/lock.c
 *
 * init(): call exactly once per mutex, from a context with no concurrent
 * access yet (the caller that owns the mutex is responsible for this —
 * runtime.c and memory/bytecode.c both init() theirs from inside their own
 * single-controller-thread init()/establish() call). destroy(): call
 * exactly once, after every lock()/unlock() use is done and before the
 * mutex's storage is freed/reused/re-init()'d — required before a second
 * init() on the same pm_metal_port_mutex_t (e.g. across an init→shutdown→
 * init cycle), since re-init()ing a live pthread_mutex_t without
 * destroy() first is undefined behavior. lock()/unlock(): ordinary
 * blocking mutex semantics, no recursion, no timeout.
 *
 * call_once(): run fn(arg) exactly once for this once object; concurrent
 * callers block until the winner finishes. mutex_ensure(): call_once that
 * runs mutex_init(m) — pair each mutex with a dedicated once. */
void pm_metal_port_mutex_init(pm_metal_port_mutex_t *m);
void pm_metal_port_mutex_destroy(pm_metal_port_mutex_t *m);
void pm_metal_port_mutex_lock(pm_metal_port_mutex_t *m);
void pm_metal_port_mutex_unlock(pm_metal_port_mutex_t *m);

void pm_metal_port_call_once(pm_metal_port_once_t *once, void (*fn)(void *), void *arg);
void pm_metal_port_mutex_ensure(pm_metal_port_mutex_t *m, pm_metal_port_once_t *once);

#endif /* PYMERGETIC_METAL_PORT_LOCK_H_ */
