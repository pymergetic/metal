/* Freestanding stub — abort from mem/libc.c; malloc via WAMR os_*. */
#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void abort(void);
void *malloc(size_t n);
void *realloc(void *p, size_t n);
void free(void *p);
void *calloc(size_t nmemb, size_t size);

int atoi(const char *s);
long atol(const char *s);
long labs(long x);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
double strtod(const char *nptr, char **endptr);
float strtof(const char *nptr, char **endptr);
void arc4random_buf(void *buf, size_t nbytes);
void qsort(void *base, size_t nmemb, size_t size,
	   int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
	      int (*compar)(const void *, const void *));

#ifdef __cplusplus
}
#endif

#endif /* _STDLIB_H */
