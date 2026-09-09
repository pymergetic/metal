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
double strtod(const char *nptr, char **endptr);
long long strtoll(const char *nptr, char **endptr, int base);
unsigned long long strtoull(const char *nptr, char **endptr, int base);
float strtof(const char *nptr, char **endptr);
long double strtold(const char *nptr, char **endptr);
int atoi(const char *s);
int abs(int x);
long labs(long x);
void qsort(void *base, size_t nmemb, size_t size, int (*cmp)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
    int (*cmp)(const void *, const void *));

/* The embedded TCC's remaining libc surface on firmware (see port/lib.c):
 * exit is the fatal-error halt (TCC's hard-error paths never fire in
 * library builds — _tcc_error is #defined to the noabort variant — but the
 * -run machinery references it); getenv has no environment to read (the
 * LD_SO probe falls through to the default interp); realpath backs
 * normalized_PATHCMP, the #pragma-once dedup that runs on every repeated
 * #include; mkstemp is jit.c's object-path temp name (the arena-backed
 * temp-file layer in port/lib.c answers it). */
void exit(int status);
char *getenv(const char *name);
char *realpath(const char *path, char *resolved_path);
int mkstemp(char *template);

#endif
