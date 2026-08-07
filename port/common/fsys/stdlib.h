#ifndef METAL_FREESTANDING_STDLIB_H
#define METAL_FREESTANDING_STDLIB_H

#include <stddef.h>

void *malloc(size_t n);
void *realloc(void *p, size_t n);
void free(void *p);
void abort(void);
int abs(int x);
long labs(long x);
void exit(int status);
int atoi(const char *s);
long strtol(const char *nptr, char **endptr, int base);
unsigned long strtoul(const char *nptr, char **endptr, int base);

#endif
