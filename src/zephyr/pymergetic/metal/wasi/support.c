/*
 * SPDX-License-Identifier: Apache-2.0
 * Zephyr WAMR WASI support — mutex/clock/rwlock + arc4random.
 * Socket APIs live in wasi/socket.c.
 */
#include "platform_api_extension.h"

#include <zephyr/kernel.h>
#include <zephyr/random/random.h>

#include <time.h>

#define PM_WASI_NS_PER_SEC 1000000000ULL

/*
 * WAMR libc-wasi picks unused fds via random_uniform() → arc4random_buf().
 * patches/wamr/0001-* sets CONFIG_HAS_ARC4RANDOM_BUF for BH_PLATFORM_ZEPHYR
 * so ssp does not fall through to open("/dev/urandom") (no Zephyr mount).
 */
void
arc4random_buf(void *buf, size_t nbytes)
{
	if (buf != NULL && nbytes > 0) {
		sys_rand_get(buf, nbytes);
	}
}

/* os_mutex_* come from WAMR zephyr_thread.c (zmutex macros under the hood). */

static __wasi_errno_t
pm_wasi_clock_value(__wasi_clockid_t clock_id, __wasi_timestamp_t *out)
{
	if (out == NULL) {
		return __WASI_EINVAL;
	}

	switch (clock_id) {
	case __WASI_CLOCK_MONOTONIC:
		*out = ( __wasi_timestamp_t)k_uptime_get() * 1000000ULL;
		return __WASI_ESUCCESS;
	case __WASI_CLOCK_REALTIME: {
		time_t now = time(NULL);

		if (now <= 0) {
#ifdef PM_METAL_BUILT_EPOCH
			*out = ( __wasi_timestamp_t)PM_METAL_BUILT_EPOCH * PM_WASI_NS_PER_SEC;
#else
			*out = 0;
#endif
		} else {
			*out = ( __wasi_timestamp_t)now * PM_WASI_NS_PER_SEC;
		}
		return __WASI_ESUCCESS;
	}
	default:
		return __WASI_ENOTSUP;
	}
}

__wasi_errno_t
os_clock_res_get(__wasi_clockid_t clock_id, __wasi_timestamp_t *resolution)
{
	(void)clock_id;
	if (resolution == NULL) {
		return __WASI_EINVAL;
	}
	*resolution = 1000000ULL;
	return __WASI_ESUCCESS;
}

__wasi_errno_t
os_clock_time_get(__wasi_clockid_t clock_id, __wasi_timestamp_t precision,
		  __wasi_timestamp_t *time)
{
	(void)precision;
	return pm_wasi_clock_value(clock_id, time);
}

/*
 * WASI fd-table / prestats rwlocks (libc-wasi locking.h → os_rwlock_*).
 * Writer-exclusive via k_mutex (korp_rwlock is zmutex_t; patches/wamr/0007-*).
 * Concurrent readers are serialized — acceptable for wasi-threads; no consumer
 * relies on shared-read parallelism.
 *
 * Lock order (FD-4): WAMR posix.c always takes ft->lock / prestats->lock
 * (these rwlocks) *before* calling os_* entry points that take
 * desc_array_mutex or the socket-table mutex. Metal never takes the WASI
 * fd-table rwlock, so there is no reverse nesting / deadlock with those
 * inner mutexes.
 */
int
os_rwlock_init(korp_rwlock *lock)
{
	if (lock == NULL) {
		return -1;
	}
	k_mutex_init(lock);
	return 0;
}

int
os_rwlock_rdlock(korp_rwlock *lock)
{
	if (lock == NULL) {
		return -1;
	}
	return k_mutex_lock(lock, K_FOREVER);
}

int
os_rwlock_wrlock(korp_rwlock *lock)
{
	if (lock == NULL) {
		return -1;
	}
	return k_mutex_lock(lock, K_FOREVER);
}

int
os_rwlock_unlock(korp_rwlock *lock)
{
	if (lock == NULL) {
		return -1;
	}
	return k_mutex_unlock(lock);
}

int
os_rwlock_destroy(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}
