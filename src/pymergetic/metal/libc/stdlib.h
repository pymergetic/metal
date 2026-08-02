#ifndef PM_METAL_LIBC_STDLIB_H_
#define PM_METAL_LIBC_STDLIB_H_

#include <stddef.h>

int atoi(const char *s);
int abs(int j);
long labs(long j);

/* WAMR bh_assert — halt firmware rather than libc abort. */
_Noreturn void abort(void);

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

#endif /* PM_METAL_LIBC_STDLIB_H_ */
