#ifndef PM_METAL_FW_STDLIB_H
#define PM_METAL_FW_STDLIB_H

#include <stddef.h>

#ifndef NULL
#define NULL ((void *)0)
#endif
void abort(void);
void *malloc(size_t n);
void free(void *p);
void *realloc(void *p, size_t n);
void *calloc(size_t nmemb, size_t size);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
int atoi(const char *s);
int abs(int x);
long labs(long x);
void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
    int (*cmp)(const void *, const void *));

#endif
