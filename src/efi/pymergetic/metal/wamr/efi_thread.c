/*
 * Single-thread stubs for WAMR extension thread/cond/sem/rwlock APIs.
 * LIB_PTHREAD=0; libc-wasi may still reference some of these.
 */
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

#include <time/time.h>

int
os_thread_create(korp_tid *p_tid, thread_start_routine_t start, void *arg,
                 unsigned int stack_size)
{
    (void)p_tid;
    (void)start;
    (void)arg;
    (void)stack_size;
    return BHT_ERROR;
}

int
os_thread_create_with_prio(korp_tid *p_tid, thread_start_routine_t start,
                           void *arg, unsigned int stack_size, int prio)
{
    (void)p_tid;
    (void)start;
    (void)arg;
    (void)stack_size;
    (void)prio;
    return BHT_ERROR;
}

int
os_thread_join(korp_tid thread, void **retval)
{
    (void)thread;
    (void)retval;
    return BHT_ERROR;
}

int
os_thread_detach(korp_tid thread)
{
    (void)thread;
    return BHT_ERROR;
}

void
os_thread_exit(void *retval)
{
    (void)retval;
}

int
os_thread_env_init(void)
{
    return BHT_OK;
}

void
os_thread_env_destroy(void)
{}

bool
os_thread_env_inited(void)
{
    return true;
}

int
os_usleep(uint32 usec)
{
    uint32_t ms;

    if (usec == 0)
        return BHT_OK;

    ms = (uint32_t)((usec + 999u) / 1000u);
    if (ms == 0)
        ms = 1;
    pm_metal_time_msleep(ms);
    return BHT_OK;
}

int
os_recursive_mutex_init(korp_mutex *mutex)
{
    return os_mutex_init(mutex);
}

int
os_cond_init(korp_cond *cond)
{
    if (cond == NULL)
        return BHT_ERROR;
    cond->dummy = 0;
    return BHT_OK;
}

int
os_cond_destroy(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

int
os_cond_wait(korp_cond *cond, korp_mutex *mutex)
{
    (void)cond;
    (void)mutex;
    /* Would deadlock on a single thread */
    return BHT_ERROR;
}

int
os_cond_reltimedwait(korp_cond *cond, korp_mutex *mutex, uint64 useconds)
{
    (void)cond;
    (void)mutex;
    (void)useconds;
    return BHT_ERROR;
}

int
os_cond_signal(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

int
os_cond_broadcast(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

int
os_rwlock_init(korp_rwlock *lock)
{
    if (lock == NULL)
        return BHT_ERROR;
    lock->dummy = 0;
    return BHT_OK;
}

int
os_rwlock_rdlock(korp_rwlock *lock)
{
    (void)lock;
    return BHT_OK;
}

int
os_rwlock_wrlock(korp_rwlock *lock)
{
    (void)lock;
    return BHT_OK;
}

int
os_rwlock_unlock(korp_rwlock *lock)
{
    (void)lock;
    return BHT_OK;
}

int
os_rwlock_destroy(korp_rwlock *lock)
{
    (void)lock;
    return BHT_OK;
}

korp_sem *
os_sem_open(const char *name, int oflags, int mode, int val)
{
    (void)name;
    (void)oflags;
    (void)mode;
    (void)val;
    return NULL;
}

int
os_sem_close(korp_sem *sem)
{
    (void)sem;
    return BHT_ERROR;
}

int
os_sem_wait(korp_sem *sem)
{
    (void)sem;
    return BHT_ERROR;
}

int
os_sem_trywait(korp_sem *sem)
{
    (void)sem;
    return BHT_ERROR;
}

int
os_sem_post(korp_sem *sem)
{
    (void)sem;
    return BHT_ERROR;
}

int
os_sem_getvalue(korp_sem *sem, int *sval)
{
    (void)sem;
    (void)sval;
    return BHT_ERROR;
}

int
os_sem_unlink(const char *name)
{
    (void)name;
    return BHT_ERROR;
}

int
os_blocking_op_init(void)
{
    return BHT_OK;
}

void
os_begin_blocking_op(void)
{}

void
os_end_blocking_op(void)
{}

int
os_wakeup_blocking_op(korp_tid tid)
{
    (void)tid;
    return BHT_OK;
}
