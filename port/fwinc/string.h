#ifndef PM_METAL_FW_STRING_H
#define PM_METAL_FW_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memchr(const void *s, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
size_t strlen(const char *s);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
size_t strnlen(const char *s, size_t maxlen);
char *strncat(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strstr(const char *hay, const char *needle);
char *strtok_r(char *str, const char *delim, char **saveptr);

/* strerror for the vendored TCC's I/O error paths (tcc_write_elf_file
 * reports open/write failures through it). One fixed string for every
 * errno: firmware I/O routes to the arena-backed temp layer in port/lib.c,
 * whose failures are "not found" — the message is diagnostic, never
 * parsed. */
char *strerror(int errnum);

#endif
