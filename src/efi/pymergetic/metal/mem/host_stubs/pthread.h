/* Freestanding stub — EFI WAMR is single-threaded. */
#ifndef _PTHREAD_H
#define _PTHREAD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef int pthread_condattr_t;
typedef int pthread_cond_t;
typedef int pthread_mutex_t;
typedef int pthread_t;

static inline int
pthread_condattr_init(pthread_condattr_t *a)
{
	(void)a;
	return -1;
}

static inline int
pthread_condattr_setclock(pthread_condattr_t *a, int clock_id)
{
	(void)a;
	(void)clock_id;
	return -1;
}

static inline int
pthread_condattr_destroy(pthread_condattr_t *a)
{
	(void)a;
	return 0;
}

static inline int
pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
	(void)c;
	(void)a;
	return -1;
}

#ifdef __cplusplus
}
#endif

#endif /* _PTHREAD_H */
