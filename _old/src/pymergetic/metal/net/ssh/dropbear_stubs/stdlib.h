#ifndef _METAL_DB_STDLIB_H
#define _METAL_DB_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void *realloc(void *p, size_t n);
void  free(void *p);
void  exit(int status);
/* Metal libc owns abort() at EFI link; Dropbear freestanding code must not emit it. */
void _exit(int status) __attribute__((noreturn));
#define abort() _exit(1)
char         *getenv(const char *name);
int           putenv(char *string);
int           unsetenv(const char *name);
int           clearenv(void);
int           atoi(const char *s);
long          atol(const char *s);
unsigned long strtoul(const char *s, char **end, int base);
long          strtol(const char *s, char **end, int base);
void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
int  abs(int x);
#endif
