#ifndef _METAL_DB_STRING_H
#define _METAL_DB_STRING_H
#include <stddef.h>
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
size_t strlen(const char *s);
char *strcpy(char *d, const char *s);
char *strncpy(char *d, const char *s, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strcat(char *d, const char *s);
char *strncat(char *d, const char *s, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *h, const char *n);
char *strdup(const char *s);
char *strerror(int errnum);
void *memchr(const void *s, int c, size_t n);
void explicit_bzero(void *s, size_t n);
size_t strlcpy(char *d, const char *s, size_t n);
char *strerror(int errnum);
size_t strlcat(char *d, const char *s, size_t n);
struct tm; /* forward — full in time.h */
#endif
