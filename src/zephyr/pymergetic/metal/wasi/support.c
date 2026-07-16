/*
 * SPDX-License-Identifier: Apache-2.0
 * Zephyr WAMR WASI support — mutex/clock/rwlock + arc4random.
 * Socket APIs live in wasi/socket.c.
 */
#include "platform_api_extension.h"
#include "platform_api_vmcore.h"

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

int
os_mutex_init(korp_mutex *mutex)
{
	if (mutex == NULL) {
		return -1;
	}
	k_mutex_init(mutex);
	return 0;
}

int
os_mutex_lock(korp_mutex *mutex)
{
	if (mutex == NULL) {
		return -1;
	}
	return k_mutex_lock(mutex, K_FOREVER);
}

int
os_mutex_unlock(korp_mutex *mutex)
{
	if (mutex == NULL) {
		return -1;
	}
	return k_mutex_unlock(mutex);
}

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
 * Zephyr WASI rwlocks are intentional no-ops for this single-threaded-per-instance
 * bring-up. They must be replaced (e.g. with k_mutex) before multi-threaded guest
 * use relies on them — concurrent readers/writers currently get no exclusion.
 */
int
os_rwlock_init(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}

int
os_rwlock_rdlock(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}

int
os_rwlock_wrlock(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}

int
os_rwlock_unlock(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}

int
os_rwlock_destroy(korp_rwlock *lock)
{
	(void)lock;
	return 0;
}
